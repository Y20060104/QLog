#pragma once

#include <cstdint>

namespace qlog::utility {
std::uint32_t utf16_to_utf8_sw(const char* src, std::uint32_t src_byte_len, char* dst,
                               std::uint32_t dst_capacity);
}