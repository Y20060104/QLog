#pragma once

#include <cstdint>

namespace qlog::utility {
// Lengths count source UTF-16 code units and destination UTF-8 bytes.
// The caller supplies aligned, non-overlapping input and enough output space.
// Returns bytes written; no terminator is appended.
std::uint32_t utf16_to_utf8_sw(const char16_t* src, std::uint32_t src_character_num, char* dst,
                               std::uint32_t dst_character_num);

std::uint32_t utf8_to_utf16_sw(const char* src, std::uint32_t src_character_num, char16_t* dst,
                               std::uint32_t dst_character_num);

}  // namespace qlog::utility