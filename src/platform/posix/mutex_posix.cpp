#include <pthread.h>

#include <cassert>
#include <cerrno>
#include <cinttypes>
#include <cstdlib>
#include <new>

#include "qlog/platform/mutex.hpp"
#include "qlog/platform/thread_ops.hpp"
#include "qlog/utility/diagnostics.hpp"

namespace qlog::platform {

struct mutex_platform_def {
    pthread_mutex_t mutex_handle;
};

mutex::mutex() : reentrant_(true) {
    platform_data_ = static_cast<mutex_platform_def*>(std::malloc(sizeof(mutex_platform_def)));

    ::new (static_cast<void*>(platform_data_)) mutex_platform_def();

    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE);  // recursive是什么？

    auto mutex_init_result = pthread_mutex_init(&platform_data_->mutex_handle, &mutex_attr);

    if (mutex_init_result != 0) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::fatal, "%s : %" PRId32 " : create mutex failed, error code:%" PRId32,
            __FILE__, __LINE__, mutex_init_result);  // file line分别是什么？

        assert(false && "create mutex failed, see the device log output for more information");
        return;
    }
}

mutex::mutex(bool reentrant) : reentrant_(reentrant) {
    platform_data_ = static_cast<mutex_platform_def*>(std::malloc(sizeof(mutex_platform_def)));

    ::new (static_cast<void*>(platform_data_)) mutex_platform_def();

    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);

    if (reentrant) {
        pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE);
    } else {
        pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_ERRORCHECK);
    }

    auto mutex_init_result = pthread_mutex_init(&platform_data_->mutex_handle, &mutex_attr);

    if (0 != mutex_init_result) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::fatal, "%s : %" PRId32 " : create mutex failed, error code:%" PRId32,
            __FILE__, __LINE__, mutex_init_result);

        assert(false && "create mutex failed, see the device log output for more information");
        return;
    }
}

mutex::~mutex() {
    auto mutex_destroy_result = pthread_mutex_destroy(&platform_data_->mutex_handle);

    if (mutex_destroy_result != 0) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::fatal, "%s : %" PRId32 " : destroy mutex failed, error code:%" PRId32,
            __FILE__, __LINE__, mutex_destroy_result);
    }

    std::free(platform_data_);
    platform_data_ = nullptr;
}

void mutex::lock() {
    auto lock_result = pthread_mutex_lock(&platform_data_->mutex_handle);

    if (0 != lock_result) {
        if (EDEADLK == lock_result) {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error,
                "%s : %" PRId32
                " : you're try to reenter a non-recursive mutex, "
                "called from thread id:%" PRIu64 ", name:\"%s\"",
                __FILE__, __LINE__, qlog::platform::get_current_thread_id(),
                qlog::platform::get_current_thread_name().c_str());

            assert(false && "mutex recursive lock");
        } else {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error,
                "%s : %" PRId32 " : pthread mutex lock error, error code:%" PRId32
                ", called from thread id:%" PRIu64 ", name:\"%s\"",
                __FILE__, __LINE__, lock_result, qlog::platform::get_current_thread_id(),
                qlog::platform::get_current_thread_name().c_str());
        }
    }
}

bool mutex::try_lock() {
    auto lock_result = pthread_mutex_trylock(&platform_data_->mutex_handle);

    if (lock_result != 0) {
        if (EBUSY == lock_result) {
            // 已上锁
        } else if (EDEADLK == lock_result) {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error,
                "%s : %" PRId32
                " : you're try to reenter a non-recursive mutex, "
                "called from thread id:%" PRIu64 ", name:\"%s\"",
                __FILE__, __LINE__, qlog::platform::get_current_thread_id(),
                qlog::platform::get_current_thread_name().c_str());

            assert(false && "mutex recursive lock");
        } else {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error,
                "%s : %" PRId32 " : pthread mutex lock error, error code:%" PRId32
                ", called from thread id:%" PRIu64 ", name:\"%s\"",
                __FILE__, __LINE__, lock_result, qlog::platform::get_current_thread_id(),
                qlog::platform::get_current_thread_name().c_str());
        }
    }
    return lock_result == 0;
}

bool mutex::unlock() {
    auto unlock_result = pthread_mutex_unlock(&platform_data_->mutex_handle);

    if (0 != unlock_result) {
        if (EPERM == unlock_result) {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error,
                "%s : %" PRId32
                " : you're try to unlock mutex does not belongs to caller thread, "
                "called from thread id:%" PRIu64 ", name:\"%s\"",
                __FILE__, __LINE__, qlog::platform::get_current_thread_id(),
                qlog::platform::get_current_thread_name().c_str());

            assert(false && "mutex recursive lock");
        } else {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error,
                "%s : %" PRId32 " : pthread mutex unlock error, error code:%" PRId32
                ", called from thread id:%" PRIu64 ", name:\"%s\"",
                __FILE__, __LINE__, unlock_result, qlog::platform::get_current_thread_id(),
                qlog::platform::get_current_thread_name().c_str());
        }
    }

    return 0 == unlock_result;
}

void* mutex::get_platform_handle() {
    return &platform_data_->mutex_handle;
}

}  // namespace qlog::platform