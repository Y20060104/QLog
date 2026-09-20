#pragma once
#include <exception>

#include "qlog/detail/worker_provider.hpp"
namespace qlog::test {
class ManualAttachment final : public detail::WorkerAttachment {
   public:
    ManualAttachment(detail::BackendSession& s, bool discard)
        : session(s), discard_on_stop(discard) {}
    detail::BackendSession& session;
    detail::BackendWorkspace workspace;
    bool discard_on_stop;
    bool failed_flag{false};
    bool released_flag{false};
    std::atomic<unsigned> notifications{0};
    bool failed() const noexcept override {
        return failed_flag;
    }
    bool failed_and_released() const noexcept override {
        return failed_flag && released_flag;
    }
    void notify() noexcept override {
        notifications.fetch_add(1U, std::memory_order_relaxed);
    }  // deliberately no consumer in producer tests
    void step() noexcept {
        (void)session.service(workspace, std::chrono::steady_clock::now());
    }
    bool wait_for_command(detail::ControlMailbox& mailbox) noexcept override {
        if (failed_flag && released_flag) return false;
        for (unsigned i = 0; i < 1000000U; ++i) {
            if (mailbox.state.load(std::memory_order_acquire) != detail::CommandState::pending)
                return true;
            step();
        }
        std::terminate();  // fixture bug, not a production timeout policy
    }
    bool finish_stop() noexcept override {
        if (failed_flag && released_flag) return false;
        if (session.detached()) return true;
        if (discard_on_stop) {
            session.abandon_after_worker_failure();
            return false;
        }
        for (unsigned i = 0; i < 1000000U; ++i) {
            if (session.service(workspace, std::chrono::steady_clock::now()) ==
                detail::SessionProgress::detach_ready) {
                session.acknowledge_detached();
                return true;
            }
        }
        std::terminate();
    }
};
class ManualProvider final : public detail::WorkerProvider {
   public:
    detail::ConsoleOutputGate gate;
    bool discard_on_stop{true};
    ManualAttachment* latest{nullptr};
    detail::ConsoleOutputGate& console_gate() noexcept override {
        return gate;
    }
    std::unique_ptr<detail::WorkerAttachment> attach(detail::BackendSession& session,
                                                     ThreadMode) override {
        auto p = std::make_unique<ManualAttachment>(session, discard_on_stop);
        latest = p.get();
        return p;
    }
};
inline ManualProvider& install_manual_worker(bool discard_on_stop = true) {
    static ManualProvider provider;
    provider.discard_on_stop = discard_on_stop;
    detail::install_worker_provider(provider);
    return provider;
}
}  // namespace qlog::test
