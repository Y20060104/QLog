#include "qlog/platform/windows/system_api.hpp"

#include <windows.h>
#include <winternl.h>

#include "qlog/global/common_vars.hpp"

namespace qlog::platform {

void* get_process_address(const char* module_name, const char* api_name) {
    HMODULE module = GetModuleHandleA(module_name);

    if (module) {
        return reinterpret_cast<void*>(GetProcAddress(module, api_name));
    }

    return nullptr;
}

static windows_version_info win_version_info_;

const windows_version_info& get_windows_version_info() {
    qlog::platform::scoped_mutex lock(qlog::common_global_vars::get().win_api_mutex_);

    if (win_version_info_.major_version == 0) {
        NTSTATUS(WINAPI * get_rtl_get_version)(PRTL_OSVERSIONINFOW) = nullptr;

        get_rtl_get_version =
            get_sys_api<decltype(get_rtl_get_version)>("ntdll.dll", "RtlGetVersion");  // 这是什么？

        if (get_rtl_get_version) {
            RTL_OSVERSIONINFOW tmp;
            NTSTATUS status = get_rtl_get_version(&tmp);
            if (status == 0) {
                win_version_info_.major_version = tmp.dwMajorVersion;
                win_version_info_.minor_version = tmp.dwMinorVersion;
                win_version_info_.build_number = tmp.dwBuildNumber;
                return win_version_info_;
            }
        }

        win_version_info_.major_version = 0;
    }

    return win_version_info_;
}

}  // namespace qlog::platform