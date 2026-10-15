#include "qlog/platform/condition_variable.hpp"

#include <pthread.h>
#include <sys/time.h>
#include <time.h>

#include <cerrno>
#include <cinttypes>
#include <cstdlib>

#include "qlog/utility/diagnostics.hpp"

#if defined(CLOCK_MONOTONIC) && !defined(__APPLE__) && !(defined(__ANDROID__) && defined(__arm__))
#define QLOG_POSIX_MONOTONIC_SUPPORTED 1
#endif

namespace qlog::platform {

struct condition_variable_platform_def {
    pthread_cond_t cond_handle;
    bool use_monotonic_clock;
};

static std::uint64_t get_current_ns(bool use_monotonic) {
    timespec ts;

#if defined(QLOG_POSIX_MONOTONIC_SUPPORTED)
    if (use_monotonic) {
        if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
            return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL +
                   static_cast<std::uint64_t>(ts.tv_nsec);
        }
    }
#endif

    (void)use_monotonic;

    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL +
               static_cast<std::uint64_t>(ts.tv_nsec);
    }

    return qlog::platform::high_performance_epoch_ms() * 1000000ULL;
}

bool condition_variable::wait_for(qlog::platform::mutex& lock, std::uint64_t wait_time_ms) {
    const std::uint64_t wait_ns_total = wait_time_ms * 1000000ULL;
    const std::uint64_t quantum_ns = 50ULL * 1000000ULL;
    const bool use_monotonic = platform_data_->use_monotonic_clock;

    std::uint64_t start_ns = get_current_ns(use_monotonic);
    std::uint64_t deadline_ns = start_ns + wait_ns_total;
    std::uint64_t next_slice_ns = start_ns + quantum_ns;

    while (true) {
        std::uint64_t now_ns = get_current_ns(use_monotonic);

        if (now_ns >= deadline_ns) {
            return false;
        }

        if (now_ns >= next_slice_ns) {
            std::uint64_t elapsed = now_ns - start_ns;
            std::uint64_t next_slice_idx = elapsed / quantum_ns + 1;

            next_slice_ns = start_ns + next_slice_idx * quantum_ns;
        }

        std::uint64_t target_ns = deadline_ns < next_slice_ns ? deadline_ns : next_slice_ns;

        timespec ts;
        ts.tv_sec = static_cast<time_t>(target_ns / 1000000000ULL);
        ts.tv_nsec = static_cast<long>(target_ns % 1000000000ULL);

        std::int32_t result = pthread_cond_timedwait(
            &platform_data_->cond_handle, (pthread_mutex_t*)lock.get_platform_handle(), &ts);

        if (result == 0) {
            return true;
        }

        if (result != ETIMEDOUT) {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error, "pthread_cond_timedwait failed: %" PRId32, result);
            return true;
        }
    }
}

condition_variable::condition_variable() {
    platform_data_ =
        (condition_variable_platform_def*)malloc(sizeof(condition_variable_platform_def));
    new (platform_data_, qlog::utility::enum_new_dummy::dummy) condition_variable_platform_def();
    platform_data_->use_monotonic_clock = false;

    pthread_condattr_t cond_attr;
    pthread_condattr_init(&cond_attr);

#if defined(QLOG_POSIX_MONOTONIC_SUPPORTED)
    // Prefer CLOCK_MONOTONIC when supported
    if (0 == pthread_condattr_setclock(&cond_attr, CLOCK_MONOTONIC)) {
        platform_data_->use_monotonic_clock = true;
    }
#endif

    std::int32_t cond_init_result = pthread_cond_init(
        &platform_data_->cond_handle, platform_data_->use_monotonic_clock ? &cond_attr : nullptr);
    if (cond_init_result != 0) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::warning,
            "pthread_cond_init with custom clock failed: %" PRId32 ", falling back",
            cond_init_result);
        platform_data_->use_monotonic_clock = false;
        cond_init_result = pthread_cond_init(&platform_data_->cond_handle, nullptr);
        if (cond_init_result != 0) {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::fatal, "%s:%" PRId32 " pthread_cond_init failed: %" PRId32,
                __FILE__, __LINE__, cond_init_result);
        }
    }

    pthread_condattr_destroy(&cond_attr);
}

condition_variable::~condition_variable() {
    if (platform_data_) {
        pthread_cond_destroy(&(platform_data_->cond_handle));
    }
    free(platform_data_);
    platform_data_ = nullptr;
}

void condition_variable::wait(qlog::platform::mutex& lock) {
    std::int32_t result = pthread_cond_wait(&platform_data_->cond_handle,
                                            (pthread_mutex_t*)(lock.get_platform_handle()));
    if (result != 0) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "pthread_cond_wait failed: %" PRId32, result);
    }
}

void condition_variable::notify_one() noexcept {
    pthread_cond_signal(&platform_data_->cond_handle);
}

void condition_variable::notify_all() noexcept {
    pthread_cond_broadcast(&platform_data_->cond_handle);
}

}  // namespace qlog::platform
