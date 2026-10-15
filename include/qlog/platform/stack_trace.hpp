#pragma once
#include <cstdint>
namespace qlog::platform {
void get_stack_trace(std::uint32_t skip_frame_count, const char*& out_str_ptr,
                     std::uint32_t& out_char_count);
void get_stack_trace_utf16(std::uint32_t skip_frame_count, const char16_t*& out_str_ptr,
                           std::uint32_t& out_char_count);
}  // namespace qlog::platform
