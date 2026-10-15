#include "qlog/platform/condition_variable.hpp"

#include <windows.h>

#include <cassert>
#include <cinttypes>

#include "qlog/platform/windows/system_api.hpp"
#include "qlog/utility/diagnostics.hpp"
namespace qlog::platform {
// Static API function pointers, zero initialization.
static VOID(WINAPI* initialize_cv_func_)(PCONDITION_VARIABLE);
static VOID(WINAPI* wake_cv_func_)(PCONDITION_VARIABLE);
static VOID(WINAPI* wake_all_cv_func_)(PCONDITION_VARIABLE);
static BOOL(WINAPI* sleep_cv_cs_func_)(PCONDITION_VARIABLE, PCRITICAL_SECTION, DWORD);

static bool native_cv_support_tested_;

// Initialize condition variable APIs - called once during startup
static void init_apis() {
    initialize_cv_func_ = qlog::platform::get_sys_api<decltype(initialize_cv_func_)>(
        "kernel32.dll", "InitializeConditionVariable");
    wake_cv_func_ = qlog::platform::get_sys_api<decltype(wake_cv_func_)>("kernel32.dll",
                                                                         "WakeConditionVariable");
    wake_all_cv_func_ = qlog::platform::get_sys_api<decltype(wake_all_cv_func_)>(
        "kernel32.dll", "WakeAllConditionVariable");
    sleep_cv_cs_func_ = qlog::platform::get_sys_api<decltype(sleep_cv_cs_func_)>(
        "kernel32.dll", "SleepConditionVariableCS");

    native_cv_support_tested_ = true;
}

// Platform definition that supports both implementations
struct condition_variable_platform_def {
    union handle_union {
        CONDITION_VARIABLE cv_;
        HANDLE condition_variable_handle_;
    };
    handle_union handle_;
};

condition_variable::condition_variable() {
    if (!native_cv_support_tested_) {
        init_apis();
    }
    platform_data_ = new condition_variable_platform_def();
    if (platform_data_) {
        if (initialize_cv_func_) {
            // Use native condition variable API
            InitializeConditionVariable(&platform_data_->handle_.cv_);
            // initialize_cv_func_(&platform_data_->handle_.cv_);
        } else {
            // Fall back to event-based implementation
            platform_data_->handle_.condition_variable_handle_ =
                CreateEvent(nullptr, FALSE, FALSE, nullptr);
            // Check if creation was successful
            if (!platform_data_->handle_.condition_variable_handle_) {
                qlog::utility::util::log_device_console(
                    qlog::LogLevel::fatal,
                    "%s : %" PRId32 " : create condition_variable failed, error code:%" PRId32,
                    __FILE__, __LINE__, static_cast<std::int32_t>(GetLastError()));
                assert(false &&
                       "create condition_variable failed, see the device log output for more "
                       "information");
                return;
            }
        }
    } else {
        assert(false && "not enough memory for condition_variable");
    }
}

condition_variable::~condition_variable() {
    if (!native_cv_support_tested_) {
        init_apis();
    }
    if (platform_data_) {
        if (initialize_cv_func_) {
        } else {
            CloseHandle(platform_data_->handle_.condition_variable_handle_);
            platform_data_->handle_.condition_variable_handle_ = nullptr;
        }
        delete platform_data_;
    }
    platform_data_ = nullptr;
}

void condition_variable::wait(qlog::platform::mutex& lock) {
    if (!native_cv_support_tested_) {
        init_apis();
    }
    if (initialize_cv_func_) {
        // Use native condition variable API
        sleep_cv_cs_func_(&platform_data_->handle_.cv_,
                          (PCRITICAL_SECTION)lock.get_platform_handle(), INFINITE);
    } else {
        // Fall back to event-based implementation
        lock.unlock();
        WaitForSingleObjectEx(platform_data_->handle_.condition_variable_handle_, INFINITE, TRUE);
        lock.lock();
    }
}

bool condition_variable::wait_for(qlog::platform::mutex& lock, std::uint64_t wait_time_ms) {
    if (!native_cv_support_tested_) {
        init_apis();
    }
    if (initialize_cv_func_) {
        // Use native condition variable API with timeout
        BOOL result =
            sleep_cv_cs_func_(&platform_data_->handle_.cv_,
                              (PCRITICAL_SECTION)lock.get_platform_handle(), (DWORD)wait_time_ms);
        return result;
    } else {
        // Fall back to event-based implementation with timeout
        lock.unlock();
        auto result = WaitForSingleObjectEx(platform_data_->handle_.condition_variable_handle_,
                                            (DWORD)wait_time_ms, TRUE);
        lock.lock();
        if (result == WAIT_TIMEOUT) {
            return false;
        }
        return true;
    }
}

void condition_variable::notify_one() noexcept {
    if (!native_cv_support_tested_) {
        init_apis();
    }
    if (initialize_cv_func_) {
        // Wake a single waiting thread using native API
        wake_cv_func_(&platform_data_->handle_.cv_);
    } else {
        // Wake a single waiting thread using event
        SetEvent(platform_data_->handle_.condition_variable_handle_);
    }
}

void condition_variable::notify_all() noexcept {
    if (!native_cv_support_tested_) {
        init_apis();
    }
    if (initialize_cv_func_) {
        // Wake all waiting threads using native API
        wake_all_cv_func_(&platform_data_->handle_.cv_);
    } else {
        // In event-based mode, this only wakes one waiting thread
        // Note: This is a limitation of the legacy implementation
        SetEvent(platform_data_->handle_.condition_variable_handle_);

        // NOTE: This implementation has a limitation in legacy mode.
        // It cannot truly wake all waiting threads with a single event.
    }
}

}  // namespace qlog::platform
