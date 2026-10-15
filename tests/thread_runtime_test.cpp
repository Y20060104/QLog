#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>

#include "qlog/platform/condition_variable.hpp"
#include "qlog/platform/thread.hpp"
class waiting_thread final : public qlog::platform::thread {
   public:
    qlog::platform::mutex mutex{true};
    qlog::platform::condition_variable trigger;
    std::atomic<bool> waiting{false};
    std::atomic<bool> done{false};

   protected:
    void run() override {
        mutex.lock();
        waiting.store(true, std::memory_order_release);
        trigger.wait(mutex, [this] { return is_cancelled(); });
        mutex.unlock();
    }
    void on_finished() override {
        done.store(true, std::memory_order_release);
    }
};
int main() {
    waiting_thread worker;
    worker.set_thread_name("QLogTest");
    worker.start();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!worker.waiting.load(std::memory_order_acquire)) {
        if (std::chrono::steady_clock::now() > deadline) {
            std::cerr << "entry timeout\n";
            return 1;
        }
        qlog::platform::thread::yield();
    }
    worker.mutex.lock();
    bool cancelled = worker.cancel();
    worker.trigger.notify_all();
    worker.mutex.unlock();
    worker.join();
    if (!cancelled || !worker.done.load() ||
        worker.get_status() != qlog::platform::enum_thread_status::released) {
        return 2;
    }
    qlog::platform::mutex lock;
    qlog::platform::condition_variable cv;
    lock.lock();
    auto begin = std::chrono::steady_clock::now();
    bool result = cv.wait_for(lock, 20, [] { return false; });
    auto elapsed = std::chrono::steady_clock::now() - begin;
    lock.unlock();
    if (result || elapsed < std::chrono::milliseconds(10) || elapsed > std::chrono::seconds(2)) {
        return 3;
    }
    std::cout << "PASS thread cancellation/condition timeout\n";
    return 0;
}
