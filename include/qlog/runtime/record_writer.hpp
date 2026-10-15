#pragma once
#include "qlog/runtime/log_types.hpp"
namespace qlog::runtime {
struct record_write_handle {
    std::uint8_t* format_data_addr = nullptr;
    enum_buffer_result_code result = enum_buffer_result_code::err_buffer_not_inited;
};
record_write_handle log_write_begin(std::uint64_t log_id, std::uint8_t log_level,
                                    std::uint32_t category_index, std::uint8_t format_string_type,
                                    std::uint32_t format_str_bytes_len, const void* format_str_data,
                                    std::uint32_t args_data_bytes_len);
void log_write_finish(std::uint64_t log_id, record_write_handle write_handle);
}  // namespace qlog::runtime
