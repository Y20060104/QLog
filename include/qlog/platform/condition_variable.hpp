#pragma once

#include <cstdint>

#include "qlog/platform/mutex.hpp"
#include "qlog/platform/time_ops.hpp"

namespace qlog::platform {

class condition_variable {
   public:
    condition_variable();

    condition_variable(const condition_variable& rhs) = delete;
    condition_variable(condition_variable&& rhs) = delete;

    ~condition_variable();

    condition_variable& operator=(const condition_variable& rhs) = delete;
    condition_variable& operator=(condition_variable&& rhs) = delete;

    void wait(qlog::platform::mutex& lock);

    template <typename Predicate>
    void wait(qlog::platform::mutex& lock, Predicate stop_waiting);

    bool wait_for(qlog::platform::mutex& lock, std::uint64_t wait_time_ms);

    template <typename Predicate>
    bool wait_for(qlog::platform::mutex& lock, std::uint64_t wait_time_ms, Predicate stop_waiting);

    void notify_one() noexcept;
    void notify_all() noexcept;

   private:
    struct condition_variable_platform_def* platform_data_;
};

template <typename Predicate>
void condition_variable::wait(qlog::platform::mutex& lock, Predicate stop_waiting) {
    while (!stop_waiting()) {
        wait(lock);
    }
}

template <typename Predicate>
bool condition_variable::wait_for(qlog::platform::mutex& lock, std::uint64_t wait_time_ms,
                                  Predicate stop_waiting) {
    std::uint64_t last_epoch = platform::high_performance_epoch_ms();
    std::uint64_t current_epoch = last_epoch;

    while (!stop_waiting()) {
        if (current_epoch >= last_epoch + wait_time_ms) {
            return false;
        }

        wait_time_ms -= current_epoch - last_epoch;

        bool result = wait_for(lock, wait_time_ms);

        if (!result) {
            return false;
        }

        last_epoch = current_epoch;
        current_epoch = platform::high_performance_epoch_ms();
    }

    return true;
}

}  // namespace qlog::platform