#pragma once

#include <cstdint>
namespace qlog::platform {

void* get_process_address(const char* module_name, const char* api_name);

template <typename API_DEC_TYPE>  // CR:dec type 是什么
API_DEC_TYPE get_sys_api(const char* module_name, const char* api_name) {
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(push)
#pragma warning(disable : 4191)  // CR：这是什么意思 warning push是什么？
#endif
    return reinterpret_cast<API_DEC_TYPE>((void*)get_process_address(module_name, api_name));
#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#endif
}

struct windows_version_info {
    std::uint32_t major_version = 0;
    std::uint32_t minor_version = 0;
    std::uint32_t build_number = 0;
};
const windows_version_info& get_windows_version_info();

}  // namespace qlog::platform