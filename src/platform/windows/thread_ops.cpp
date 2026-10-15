#include "qlog/platform/thread_ops.hpp"

#include <windows.h>

#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <vector>

#include "qlog/platform/thread.hpp"
#include "qlog/platform/windows/system_api.hpp"
#include "qlog/utility/diagnostics.hpp"

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace qlog::platform {
namespace {
thread_local thread_id current_thread_id_ = 0;
}

thread_id get_current_thread_id() {
    if (current_thread_id_ == 0) {
        current_thread_id_ = static_cast<thread_id>(
            ::GetCurrentThreadId());  // CR:我记得BQLog是自己维护了单增的tid啊
    }
    return current_thread_id_;
}

void yield() {
    ::SwitchToThread();
}

void cpu_relax() {
#if defined(_MSC_VER)
#if defined(_M_IX86) || defined(_M_X64)
    _mm_pause();
#elif defined(_M_ARM) || defined(_M_ARM64)
    __yield();
#else
    _ReadWriteBarrier();
#endif
#else
#if defined(__arm__) || defined(__aarch64__)
    __asm__ __volatile__("yield");
#else
    __asm__ __volatile__("pause");
#endif
#endif
}

void sleep(std::uint64_t millsec) {
    ::SleepEx(static_cast<DWORD>(millsec), TRUE);
}

static bool is_thread_name_supported_tested_;
static HRESULT(WINAPI* get_thread_desc_func_)(HANDLE, PWSTR*);
static HRESULT(WINAPI* set_thread_desc_func_)(HANDLE, PCWSTR);

static void init_thread_apis() {
    const auto& os_ver_info = qlog::platform::get_windows_version_info();

    if (os_ver_info.major_version > 10 ||
        (os_ver_info.major_version == 10 && os_ver_info.build_number >= 14393)) {
        get_thread_desc_func_ = qlog::platform::get_sys_api<decltype(get_thread_desc_func_)>(
            "kernel32.dll", "GetThreadDescription");
        set_thread_desc_func_ = qlog::platform::get_sys_api<decltype(set_thread_desc_func_)>(
            "kernel32.dll", "SetThreadDescription");
    }

    is_thread_name_supported_tested_ = true;
}

std::string get_current_thread_name() {
    if (!is_thread_name_supported_tested_) {
        init_thread_apis();
    }

    if (get_thread_desc_func_) {
        HANDLE current_thread_handle = ::GetCurrentThread();
        PWSTR raw_thread_name;

        HRESULT result = get_thread_desc_func_(current_thread_handle, &raw_thread_name);

        if (SUCCEEDED(result)) {
            std::int32_t utf8_len = ::WideCharToMultiByte(CP_UTF8, 0, raw_thread_name, -1, nullptr,
                                                          0, nullptr, nullptr);

            assert(utf8_len >= 1);
            --utf8_len;

            if (utf8_len == 0) {
                return "";
            }

            std::string thread_name_utf8;
            thread_name_utf8.resize(static_cast<std::size_t>(utf8_len));

            if (!::WideCharToMultiByte(CP_UTF8, 0, raw_thread_name,
                                       static_cast<std::int32_t>(std::wcslen(raw_thread_name)),
                                       thread_name_utf8.data(), utf8_len, nullptr, nullptr)) {
                return "";
            }
            return thread_name_utf8;
        }
    }

    char tmp[64];
    std::snprintf(tmp, sizeof(tmp), "THREAD-%" PRIu64, get_current_thread_id());
    return tmp;
}

struct thread_platform_def {
    qlog::platform::atomic<HANDLE> thread_handle = INVALID_HANDLE_VALUE;
};

struct thread_platform_processor {
    static DWORD thread_process(LPVOID data) {
        thread* thread_ptr = static_cast<thread*>(data);

        thread_ptr->thread_id_ = thread::get_current_thread_id();
        thread_ptr->internal_run();

        return 0;
    }
};

thread::thread(thread_attr attr) : attr_(attr), status_(enum_thread_status::init) {
    thread_id_ = 0;

    platform_data_ = static_cast<thread_platform_def*>(malloc(sizeof(thread_platform_def)));

    new (platform_data_, qlog::utility::enum_new_dummy::dummy) thread_platform_def();
}

void thread::internal_run() {
    while (status_.load() != enum_thread_status::running &&
           status_.load() != enum_thread_status::pendding_cancel &&
           status_.load() != enum_thread_status::detached) {
        cpu_relax();
    }

    apply_thread_name();
    run();

    auto expected_status = enum_thread_status::running;

    if (!status_.compare_exchange_strong(expected_status, enum_thread_status::finished,
                                         memory_order::seq_cst, memory_order::seq_cst)) {
        expected_status = enum_thread_status::pendding_cancel;

        status_.compare_exchange_strong(expected_status, enum_thread_status::finished,
                                        memory_order::seq_cst, memory_order::seq_cst);
    }

    on_finished();

    thread_id_ = 0;

    qlog::utility::util::log_device_console(qlog::LogLevel::info, "thread cancel success");

    status_.store_seq_cst(enum_thread_status::released);
}

#pragma pack(push, 8)
typedef struct tagTHREADNAME_INFO {
    DWORD dwType;      // Must be 0x1000.
    LPCSTR szName;     // Pointer to name (in user addr space).
    DWORD dwThreadID;  // Thread ID (-1=caller thread).
    DWORD dwFlags;     // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)

void thread::set_thread_name(const std::string& thread_name) {
    thread_name_ = thread_name;
    if (thread_name_.size() >= 15) {
        std::string truncated_thread_name = thread_name_.substr(0, 15);
        qlog::utility::util::log_device_console(
            qlog::LogLevel::warning,
            "thread name \"%s\" excceed max length limit of android, the length of thread name can "
            "not be larger than 15. so it will be truncated to \"%s\"",
            thread_name_.c_str(), truncated_thread_name.c_str());
        thread_name_ = truncated_thread_name;
    }
    auto current_status = status_.load();
    if (current_status == enum_thread_status::running) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::warning,
            "trying to set thread name \"%s\" when thread have already been running, thread id "
            ":%" PRIu64 ", thread status:%" PRId32,
            thread_name.c_str(), static_cast<std::uint64_t>(thread_id_),
            (std::int32_t)current_status);
    }
}

void thread::start() {
    auto current_status = status_.load();
    if (current_status == enum_thread_status::running) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::warning,
            "trying to start a thread \"%s\" which is still running, thread id :%" PRIu64
            ", thread status : %" PRId32,
            thread_name_.c_str(), static_cast<std::uint64_t>(thread_id_),
            (std::int32_t)current_status);
        return;
    }
    if (current_status == enum_thread_status::detached) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::fatal,
            "trying to start a thread \"%s\" which is detached, thread id :%" PRIu64
            ", thread status:%" PRId32,
            thread_name_.c_str(), static_cast<std::uint64_t>(thread_id_),
            (std::int32_t)current_status);
        assert(false && "trying to start a detached thread");
        return;
    }
    if (platform_data_->thread_handle.load() != INVALID_HANDLE_VALUE) {
        CloseHandle(platform_data_->thread_handle.load());
        platform_data_->thread_handle.store_seq_cst(INVALID_HANDLE_VALUE);
    }
    platform_data_->thread_handle =
        CreateThread(NULL, attr_.max_stack_size, &thread_platform_processor::thread_process, this,
                     STACK_SIZE_PARAM_IS_A_RESERVATION, (DWORD*)&thread_id_);
    if (!platform_data_->thread_handle.load()) {
        status_.store_seq_cst(enum_thread_status::error);
        qlog::utility::util::log_device_console(
            qlog::LogLevel::fatal, "create thread \"%s\" failed, error code:%" PRId32,
            thread_name_.c_str(), static_cast<std::int32_t>(GetLastError()));
        assert(false && "create thread failed, see the device log output for more information");
        return;
    }

    // If the previous status is init or released, we can set it to running.
    // internal_run() is spinning and waiting for this status change.
    auto expected_status = enum_thread_status::init;
    if (!status_.compare_exchange_strong(expected_status, enum_thread_status::running)) {
        expected_status = enum_thread_status::released;
        status_.compare_exchange_strong(expected_status, enum_thread_status::running);
    }
}

void thread::join() {
    auto current_status = status_.load();
    if (!platform_data_->thread_handle.load() ||
        platform_data_->thread_handle.load() == INVALID_HANDLE_VALUE) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::warning,
            "trying to join a thread \"%s\" which is not started or is ended, thread id :%" PRIu64
            ", thread status : %" PRId32,
            thread_name_.c_str(), static_cast<std::uint64_t>(thread_id_),
            (std::int32_t)current_status);
        return;
    }
    DWORD wait_result = WaitForSingleObjectEx(platform_data_->thread_handle.load(), INFINITE, true);
    if (wait_result != WAIT_OBJECT_0) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::error,
            "join thread \"%s\" failed, thread_id:%" PRIu64 ", error code : %" PRId32
            ", return value : %" PRId32,
            thread_name_.c_str(), static_cast<std::uint64_t>(thread_id_),
            static_cast<std::int32_t>(GetLastError()), static_cast<std::int32_t>(wait_result));
    }
}

void thread::detach() {
    auto current_status = status_.load();
    if (current_status != enum_thread_status::running) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::warning,
            "trying to detach a thread \"%s\" which is not running, thread id :%" PRIu64
            ", thread status:%" PRId32,
            thread_name_.c_str(), static_cast<std::uint64_t>(thread_id_),
            (std::int32_t)current_status);
        return;
    }
    if (platform_data_->thread_handle.load() != INVALID_HANDLE_VALUE) {
        CloseHandle(platform_data_->thread_handle.load());
        platform_data_->thread_handle.store_seq_cst(INVALID_HANDLE_VALUE);
    }
    status_.store_seq_cst(enum_thread_status::detached);
}

thread::~thread() {
    if (platform_data_->thread_handle.load() != INVALID_HANDLE_VALUE) {
        if (!CloseHandle(platform_data_->thread_handle.load())) {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error,
                "Win64 Thread CloseHandle failed, GetLastError=%" PRId32
                ", thread name:%s, thread_id:%" PRIu64,
                static_cast<std::int32_t>(GetLastError()), thread_name_.c_str(), thread_id_);
        }
    }
    platform_data_->~thread_platform_def();
    free(platform_data_);
    platform_data_ = nullptr;
}

void thread::apply_thread_name() {
    if (!is_thread_name_supported_tested_) {
        init_thread_apis();
    }
    if (set_thread_desc_func_) {
        HANDLE current_thread_handle = GetCurrentThread();
        std::int32_t required_size =
            MultiByteToWideChar(CP_UTF8, 0, thread_name_.c_str(),
                                static_cast<std::int32_t>(thread_name_.size()), nullptr, 0);
        std::vector<wchar_t> wide_str;
        wide_str.resize(static_cast<std::size_t>(required_size + 1));
        wide_str[required_size] = 0;
        std::int32_t converted_size = MultiByteToWideChar(
            CP_UTF8, 0, thread_name_.c_str(), static_cast<std::int32_t>(thread_name_.size()),
            &wide_str[0], required_size);
        if (converted_size == required_size) {
            set_thread_desc_func_(current_thread_handle, &wide_str[0]);
        }
    } else {
        apply_name_raise();
    }
}

void thread::apply_name_raise() {
#if defined(_MSC_VER)
    THREADNAME_INFO info;
    info.dwType = 0x1000;
    info.szName = thread_name_.c_str();
    info.dwThreadID = (DWORD)-1;
    info.dwFlags = 0;

#pragma warning(push)
#pragma warning(disable : 6320 6322)
    __try {
        const DWORD MS_VC_EXCEPTION = 0x406D1388;
        RaiseException(MS_VC_EXCEPTION, 0, static_cast<DWORD>(sizeof(info) / sizeof(ULONG_PTR)),
                       (ULONG_PTR*)&info);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
#pragma warning(pop)
#else
    qlog::utility::util::log_device_console(
        qlog::LogLevel::warning,
        "Set thread name is only support Windows 10 or later, or compiled with MSVC");
#endif
}

}  // namespace qlog::platform
