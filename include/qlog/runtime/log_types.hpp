#pragma once
#include <algorithm>
#include <bit>
#include <cassert>
#include <cerrno>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "qlog/buffer/log_buffer.hpp"
#include "qlog/buffer/normal_buffer.hpp"
#include "qlog/buffer/siso_ring_buffer.hpp"
#include "qlog/config/buffer_config.hpp"
#include "qlog/config/filter_config.hpp"
#include "qlog/config/property_value.hpp"
#include "qlog/io/file_manager.hpp"
#include "qlog/layout/layout.hpp"
#include "qlog/platform/condition_variable.hpp"
#include "qlog/platform/memory_map.hpp"
#include "qlog/platform/thread.hpp"
#include "qlog/platform/time_ops.hpp"
#include "qlog/runtime/log_level_bitmap.hpp"
#include "qlog/utility/alignment.hpp"
#include "qlog/utility/diagnostics.hpp"
#include "qlog/utility/string_utils.hpp"
namespace qlog {
struct log_global_vars;
class log;
}  // namespace qlog
#if defined(_WIN32)
#define QLOG_STDCALL __stdcall
#else
#define QLOG_STDCALL
#endif
namespace qlog::runtime {
using string = std::string;
using property_value = qlog::config::PropertyValue;
using layout = qlog::layout::Layout;
using time_zone = qlog::layout::TimeZone;
using log_level_bitmap = LogLevelBitmap;
using log_entry_handle = qlog::record::LogEntryHandle;
using log_buffer_config = qlog::buffer::log_buffer_config;
using log_memory_policy = qlog::buffer::log_memory_policy;
using enum_buffer_result_code = qlog::buffer::enum_buffer_result_code;
using siso_ring_buffer = qlog::buffer::siso_ring_buffer;
using normal_buffer = qlog::buffer::normal_buffer;
using file_handle = qlog::file_handle;
using file_manager = qlog::file_manager;
using file_open_mode_enum = qlog::file_open_mode_enum;
using log_global_vars = qlog::log_global_vars;
using qlog::buffer::scoped_log_buffer_handle;
enum class log_thread_mode { sync, async, independent };
using type_func_ptr_console_callback = void(QLOG_STDCALL*)(std::uint64_t, std::int32_t,
                                                           qlog::LogLevel, const char*,
                                                           std::int32_t);
using type_func_ptr_console_buffer_fetch_callback = void(QLOG_STDCALL*)(void*, std::uint64_t,
                                                                        std::int32_t,
                                                                        qlog::LogLevel, const char*,
                                                                        std::int32_t);
}  // namespace qlog::runtime
