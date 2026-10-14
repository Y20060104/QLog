#pragma once

#include <cstddef>
#include <vector>

#include "qlog/config/property_value.hpp"
#include "qlog/global/global_vars.hpp"
#include "qlog/io/file_manager.hpp"
#include "qlog/platform/atomic/atomic.hpp"
#include "qlog/platform/file_defs.hpp"
#include "qlog/platform/file_path.hpp"
#include "qlog/platform/mutex.hpp"
#include "qlog/utility/hash_map.hpp"

namespace qlog {
// CR:common_global_vars是什么意思?
struct common_global_vars : public global_vars_base<common_global_vars> {
    std::size_t page_size_;  // CR: common global vars 为什么public <x> 也是common...

#if defined(_WIN32)
    qlog::platform::mutex win_api_mutex_;
#endif

    std::vector<char> device_console_buffer_ = {'\0'};
    qlog::platform::mutex console_mutex_;

#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    bool avx2_support_;
#endif

    bool crc32_supported_;

    qlog::platform::base_dir_initializer base_dir_init_inst_;

    qlog::utility::hash_map<qlog::platform::file_node_info, qlog::platform::file_open_mode_enum>
        file_exclusive_cache_;
    // CR:下面两是什么
    qlog::platform::mutex file_exclusive_mutex_;
    qlog::platform::mutex stack_trace_mutex_;

#if defined(_WIN32)
    qlog::platform::platform_file_handle stack_trace_process_ =
        qlog::platform::invalid_platform_file_handle;

    qlog::platform::atomic<bool> stack_trace_sym_initialized_ = false;
#endif

    qlog::config::PropertyValue null_property_value_;

    file_manager* file_manager_inst_ = new file_manager();

    common_global_vars();

   protected:
    virtual void partial_destruct() override;
};
}  // namespace qlog