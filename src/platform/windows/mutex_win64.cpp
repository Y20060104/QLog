#include <windows.h>

#include <cassert>
#include <cinttypes>
#include <cstdlib>
#include <new>

#include "qlog/platform/atomic/atomic.hpp"
#include "qlog/platform/mutex.hpp"
#include "qlog/platform/thread_ops.hpp"
#include "qlog/utility/diagnostics.hpp"

namespace qlog::platform {

struct mutex_platform_def {
    CRITICAL_SECTION cs_;
    qlog::platform::atomic<thread_id> owner_thread_id_;

    mutex_platform_def() : cs_(CRITICAL_SECTION()), owner_thread_id_(0) {}
};

mutex::mutex() : reentrant_(true) {
    platform_data_ = static_cast<mutex_platform_def*>(std::malloc(sizeof(mutex_platform_def)));

    if (platform_data_) {
        ::new (static_cast<void*>(platform_data_)) mutex_platform_def();
        platform_data_->owner_thread_id_.store_seq_cst(0);
        InitializeCriticalSection(&platform_data_->cs_);
    } else {
        assert(false && "not enough memory for mutex");
    }
}

mutex::mutex(bool reentrant) : reentrant_(reentrant) {
    platform_data_ = static_cast<mutex_platform_def*>(std::malloc(sizeof(mutex_platform_def)));

    if (platform_data_) {
        ::new (static_cast<void*>(platform_data_)) mutex_platform_def();
        platform_data_->owner_thread_id_.store_seq_cst(0);
        InitializeCriticalSection(&platform_data_->cs_);
    } else {
        assert(false && "not enough memory for mutex");
    }
}

mutex::~mutex() {
    DeleteCriticalSection(&platform_data_->cs_);
    std::free(platform_data_);
    platform_data_ = nullptr;
}

void mutex::lock() {
    EnterCriticalSection(&platform_data_->cs_);

    if (!reentrant_) {
        qlog::platform::thread_id current_thread_id = qlog::platform::get_current_thread_id();

        if (platform_data_->owner_thread_id_.exchange_relaxed(current_thread_id) ==
            current_thread_id) {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error,
                "%s : %" PRId32 " : you're try to reenter a non-recursive mutex", __FILE__,
                __LINE__);

            // assert(false && "mutex recursive lock");
        }
    }
}

bool mutex::try_lock() {
    if (TryEnterCriticalSection(&platform_data_->cs_)) {
        if (!reentrant_) {
            qlog::platform::thread_id current_thread_id = qlog::platform::get_current_thread_id();

            if (platform_data_->owner_thread_id_.exchange_relaxed(current_thread_id) ==
                current_thread_id) {
                qlog::utility::util::log_device_console(
                    qlog::LogLevel::error,
                    "%s : %" PRId32 " : you're try to reenter a non-recursive mutex", __FILE__,
                    __LINE__);

                // assert(false && "mutex recursive lock");
            }
        }

        return true;
    } else {
        return false;
    }
}

bool mutex::unlock() {
    platform_data_->owner_thread_id_.store(0, memory_order::release);
    LeaveCriticalSection(&platform_data_->cs_);
    return true;
}

void* mutex::get_platform_handle() {
    return &platform_data_->cs_;
}
// CRITICAL_SECTION有什么用？ 为什么是旧owner？ 由当前线程重入是什么？
}  // namespace qlog::platform