#include "qlog/platform/thread_ops.hpp"

#include <windows.h>

#include <cassert>
#include <cinttypes>
#include <cstdio>
#include <cwchar>

#include "qlog/platform/windows/system_api.hpp"

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
}  // namespace qlog::platform