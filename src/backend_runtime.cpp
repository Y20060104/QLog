#include <pthread.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <thread>

#include "qlog/detail/worker_provider.hpp"

namespace qlog::detail {
namespace {
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

// Stable registration node. Engine owns published nodes until its thread joins.
struct Node {
    explicit Node(BackendSession& value) : session(&value) {}
    BackendSession* session;
    Node* published_next{nullptr};  // immutable after CAS publication
    Node* next{nullptr};            // worker-only active link
    std::atomic<bool> attached{false}, released{false}, failed{false}, waiting{false};
    std::mutex mutex;
    std::condition_variable completion;
    void signal() noexcept {
        if (waiting.load(std::memory_order_seq_cst)) {
            std::lock_guard lock(mutex);
            completion.notify_all();
        }
    }
};

class Engine {
   public:
    Engine() : thread_([this] { run(); }) {
        std::unique_lock lock(mutex_);
        wake_.wait(lock, [this] { return ready_; });
        const int error = startup_error_;
        lock.unlock();
        if (error != 0) {
            thread_.join();
            throw std::system_error(error, std::generic_category(), "worker SIGPIPE setup");
        }
    }
    ~Engine() {
        stop_join();
        auto* node = published_.load(std::memory_order_relaxed);
        while (node) {
            auto* next = node->published_next;
            delete node;
            node = next;
        }
    }
    void stop_join() noexcept {
        stop_.store(true, std::memory_order_release);
        notify();
        if (thread_.joinable()) thread_.join();
    }
    void notify() noexcept {
        // BQLog-style pressure wakeup. A pre-arm notification is covered by the
        // finite tick. A post-arm notification pairs with the same wait mutex.
        if (waiting_.exchange(false, std::memory_order_relaxed)) {
            std::lock_guard lock(mutex_);
            wake_.notify_one();
        }
    }
    Node* attach(std::unique_ptr<Node> prepared) {
        auto count = registrations_.load(std::memory_order_acquire);
        for (;;) {
            if (count & kClosed) throw std::runtime_error("QLog worker stopped");
            if (registrations_.compare_exchange_weak(count, count + 1, std::memory_order_acq_rel,
                                                     std::memory_order_acquire))
                break;
        }
        auto* node = prepared.release();  // engine owns the node from publication
        auto* head = published_.load(std::memory_order_relaxed);
        do {
            node->published_next = head;
        } while (!published_.compare_exchange_weak(head, node, std::memory_order_release,
                                                   std::memory_order_relaxed));
        const auto prior = registrations_.fetch_sub(1, std::memory_order_acq_rel);
        if ((prior & kClosed) && (prior & ~kClosed) == 1) {
            std::lock_guard lock(mutex_);
            wake_.notify_all();  // failed worker waiting for in-flight publishers
        }
        notify();
        std::unique_lock lock(node->mutex);
        node->waiting.store(true);
        node->completion.wait(lock, [&] {
            return node->attached.load(std::memory_order_acquire) ||
                   node->released.load(std::memory_order_acquire);
        });
        node->waiting.store(false);
        if (node->failed.load(std::memory_order_acquire))
            throw std::runtime_error("QLog worker failed while attaching");
        return node;
    }

   private:
    BackendWorkspace workspace_;  // allocate before thread construction
    std::mutex mutex_;
    std::condition_variable wake_;
    std::atomic<bool> waiting_{false}, stop_{false};
    static constexpr std::uint64_t kClosed = std::uint64_t{1} << 63U;
    std::atomic<std::uint64_t> registrations_{0};
    std::atomic<Node*> published_{nullptr};
    Node* known_head_{nullptr};
    Node* snapshot_head_{nullptr};
    Node* discover_cursor_{nullptr};
    bool ready_{false};
    int startup_error_{0};
    std::thread thread_;  // last: every accessed member must already exist

    static void release(Node& node, bool failure) noexcept {
        std::lock_guard lock(node.mutex);
        node.failed.store(failure, std::memory_order_release);
        node.released.store(true, std::memory_order_release);
        node.completion.notify_all();
        // Session is released; stable node storage remains owned by Engine.
    }
    bool import(Node*& active) noexcept {
        if (!discover_cursor_) {
            snapshot_head_ = published_.load(std::memory_order_acquire);
            if (snapshot_head_ == known_head_) return false;
            discover_cursor_ = snapshot_head_;
        }
        for (unsigned count = 0; count < 64U; ++count) {
            auto* node = discover_cursor_;
            discover_cursor_ = node->published_next;
            node->next = active;
            active = node;
            node->attached.store(true, std::memory_order_release);
            node->signal();
            if (discover_cursor_ == known_head_) {
                known_head_ = snapshot_head_;
                discover_cursor_ = nullptr;
                return published_.load(std::memory_order_acquire) != known_head_;
            }
        }
        return true;
    }
    void run() noexcept {
        sigset_t signals;
        ::sigemptyset(&signals);
        ::sigaddset(&signals, SIGPIPE);
        const int error = ::pthread_sigmask(SIG_BLOCK, &signals, nullptr);
        {
            std::lock_guard lock(mutex_);
            startup_error_ = error;
            ready_ = true;
            wake_.notify_all();
        }
        if (error) return;
        Node* active = nullptr;
        try {
            while (!stop_.load(std::memory_order_acquire)) {
                bool runnable = import(active);
                auto deadline = Clock::now() + 66ms;
                auto** link = &active;
                while (*link) {
                    auto* node = *link;
                    auto* session = node->session;
                    const auto progress = session->service(workspace_, Clock::now());
                    if (progress == SessionProgress::detach_ready) {
                        *link = node->next;
                        node->session = nullptr;
                        session->acknowledge_detached();  // LAST Session access
                        release(*node, false);
                        continue;
                    }
                    runnable |= progress == SessionProgress::runnable;
                    deadline = std::min(deadline, session->next_deadline());
                    node->signal();
                    link = &node->next;
                }
                if (runnable) continue;
                // Expired/retry deadlines must not cause a zero-timeout spin.
                deadline = std::max(deadline, Clock::now() + 1ms);
                std::unique_lock lock(mutex_);
                waiting_.store(true, std::memory_order_relaxed);
                wake_.wait_until(lock, deadline, [this] {
                    return !waiting_.load(std::memory_order_relaxed) || stop_.load() ||
                           published_.load(std::memory_order_acquire) != known_head_;
                });
                waiting_.store(false, std::memory_order_relaxed);
            }
        } catch (...) {
            // No Session::service exception escapes (noexcept). This contains
            // scheduler/wait failures, then proves every borrow is released.
        }
        // Close publication, then wait for publishers that acquired admission
        // before the close bit. This also covers failure concurrent with attach.
        registrations_.fetch_or(kClosed, std::memory_order_acq_rel);
        {
            std::unique_lock lock(mutex_);
            wake_.wait(lock, [this] {
                return (registrations_.load(std::memory_order_acquire) & ~kClosed) == 0;
            });
        }
        active = nullptr;
        discover_cursor_ = nullptr;
        for (auto* node = published_.load(std::memory_order_acquire); node;
             node = node->published_next) {
            if (!node->released.load(std::memory_order_acquire)) {
                node->session = nullptr;
                release(*node, true);
            }
        }
    }
};

class Attachment final : public WorkerAttachment {
   public:
    Attachment(BackendSession& session, Engine* shared) {
        if (shared)
            engine_ = shared;
        else {
            owned_ = std::make_unique<Engine>();
            engine_ = owned_.get();
        }
        node_ = engine_->attach(std::make_unique<Node>(session));
    }
    ~Attachment() noexcept override {
        if (!node_->released.load(std::memory_order_acquire)) std::terminate();
    }
    void notify() noexcept override {
        engine_->notify();
    }
    bool failed() const noexcept override {
        return node_->failed.load(std::memory_order_acquire);
    }
    bool failed_and_released() const noexcept override {
        return node_->released.load(std::memory_order_acquire) && failed();
    }
    bool wait_for_command(ControlMailbox& mailbox) noexcept override {
        std::unique_lock lock(node_->mutex);
        node_->waiting.store(true);
        node_->completion.wait(lock, [&] {
            return mailbox.state.load(std::memory_order_acquire) == CommandState::completed ||
                   node_->released.load(std::memory_order_acquire);
        });
        node_->waiting.store(false);
        const bool success =
            mailbox.state.load(std::memory_order_acquire) == CommandState::completed;
        lock.unlock();
        if (!success && owned_) owned_->stop_join();
        return success;
    }
    bool finish_stop() noexcept override {
        std::unique_lock lock(node_->mutex);
        node_->completion.wait(lock,
                               [&] { return node_->released.load(std::memory_order_acquire); });
        lock.unlock();
        if (owned_) owned_->stop_join();
        return !failed();
    }

   private:
    Node* node_{};
    std::unique_ptr<Engine> owned_;
    Engine* engine_{};
};

class Runtime final : public WorkerProvider {
   public:
    ConsoleOutputGate& console_gate() noexcept override {
        return console_;
    }
    std::unique_ptr<WorkerAttachment> attach(BackendSession& session, ThreadMode mode) override {
        if (mode == ThreadMode::independent) return std::make_unique<Attachment>(session, nullptr);
        Engine* engine;
        {
            std::lock_guard lock(mutex_);
            if (!shared_) shared_ = std::make_unique<Engine>();
            engine = shared_.get();
        }
        return std::make_unique<Attachment>(session, engine);
    }

   private:
    ConsoleOutputGate console_;
    std::mutex mutex_;
    std::unique_ptr<Engine> shared_;
};
}  // namespace
WorkerProvider& default_worker_provider() {
    static Runtime runtime;
    return runtime;
}
}  // namespace qlog::detail
