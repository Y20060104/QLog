/* Copyright (C) 2025 Tencent.
 * BQLOG is licensed under the Apache License, Version 2.0.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 */
#pragma once
#include <cstddef>
#include <cstdint>
namespace qlog::utility {
std::uint32_t utf16_to_utf_mixed(const char16_t* src, std::uint32_t src_character_num, char* dst,
                                 std::uint32_t dst_character_num);
std::uint32_t utf_mixed_to_utf8(const char* src, std::uint32_t src_character_num, char* dst,
                                std::uint32_t dst_character_num);
std::uint64_t hash_utf_mixed_as_utf16(const void* mixed, std::size_t len);
}  // namespace qlog::utility
