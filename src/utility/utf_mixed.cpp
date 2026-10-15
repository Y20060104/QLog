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
#include "qlog/utility/utf_mixed.hpp"

#include <cassert>
#include <cstring>
#include <vector>

#include "qlog/platform/macros.hpp"
#include "qlog/utility/aligned_allocator.hpp"
#include "qlog/utility/hash.hpp"
#include "qlog/utility/utf_conversion.hpp"
namespace qlog::utility {
uint32_t utf16_to_utf_mixed(const char16_t* src, uint32_t src_character_num, char* dst,
                            uint32_t dst_character_num) {
    uint32_t written_character_num = 0;
    while (written_character_num < src_character_num && src[written_character_num] <= 0x7F) {
        dst[written_character_num] = static_cast<char>(src[written_character_num]);
        ++written_character_num;
    }
    QLOG_UNLIKELY_IF(written_character_num >= dst_character_num) {
        assert(false && "utf16_to_utf_mixed dst space utf8 not enough");
    }
    QLOG_LIKELY_IF(written_character_num == src_character_num) {
        return written_character_num;
    }
    dst[written_character_num] = static_cast<char>(static_cast<unsigned char>(0xFF));
    QLOG_UNLIKELY_IF(written_character_num < src_character_num) {
        auto left_chars = src_character_num - written_character_num;
        auto left_space = dst_character_num - (written_character_num + 1);
        QLOG_UNLIKELY_IF(left_space < left_chars * sizeof(char16_t)) {
            assert(false && "utf16_to_utf_mixed dst space utf16 not enough");
        }
        memcpy(dst + written_character_num + 1, src + written_character_num,
               left_chars * sizeof(char16_t));
        written_character_num += static_cast<uint32_t>(left_chars * sizeof(char16_t));
    }
    return written_character_num + 1;
}

using mixed_aligned_utf16_buffer_type = std::vector<char, aligned_allocator<char, 8>>;
QLOG_TLS_NON_POD(mixed_aligned_utf16_buffer_type, mixed_utf16_buffer_)
uint32_t utf_mixed_to_utf8(const char* src, uint32_t src_character_num, char* dst,
                           uint32_t dst_character_num) {
    auto mark_ptr =
        memchr(static_cast<const void*>(src), 0xFF, static_cast<size_t>(src_character_num));
    QLOG_LIKELY_IF(!mark_ptr) {
        memcpy(dst, src, src_character_num);
        return src_character_num;
    }
    auto utf8_len = static_cast<uint32_t>(static_cast<const char*>(mark_ptr) - src);
    QLOG_UNLIKELY_IF(utf8_len > dst_character_num) {
        assert(false && "utf_mixed_to_utf8 dst space utf8 not enough");
    }
    memcpy(dst, src, utf8_len);
    auto left_space = static_cast<uint32_t>(dst_character_num - utf8_len);
    auto left_characters = src_character_num - utf8_len - 1;
    const char* utf16_start = src + utf8_len + 1;
    auto& buffer = mixed_utf16_buffer_.get();
    if ((reinterpret_cast<uintptr_t>(utf16_start) & static_cast<uintptr_t>(0x01)) != 0) {
        buffer.clear();
        buffer.resize(left_characters);
        memcpy(buffer.data(), utf16_start, left_characters);
        utf16_start = buffer.data();
    }
    utf8_len += utf16_to_utf8_sw(reinterpret_cast<const char16_t*>(utf16_start),
                                 left_characters >> 1, dst + utf8_len, left_space);
    if (buffer.size() > 256) {
        mixed_aligned_utf16_buffer_type replacement;
        replacement.reserve(256);
        buffer.swap(replacement);
    }
    return utf8_len;
}
uint64_t hash_utf_mixed_as_utf16(const void* mixed, size_t len) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(mixed);
    const uint8_t* split = static_cast<const uint8_t*>(memchr(p, 0xFF, len));
    size_t ascii_len = split ? static_cast<size_t>(split - p) : len;
    size_t suffix_len = split ? (len - ascii_len - 1) : 0;
    size_t total_utf16_bytes = ascii_len * 2 + suffix_len;
    auto& buf = mixed_utf16_buffer_.get();
    if (buf.size() < total_utf16_bytes) {
        buf.clear();
        buf.resize(total_utf16_bytes);
    }
    char16_t* dst = reinterpret_cast<char16_t*>(buf.data());
    for (size_t i = 0; i < ascii_len; ++i) {
        *dst++ = static_cast<char16_t>(p[i]);
    }
    if (suffix_len > 0) {
        memcpy(dst, split + 1, suffix_len);
    }
    uint64_t hash_result = get_hash_64(buf.data(), total_utf16_bytes);
    if (buf.size() > 256) {
        mixed_aligned_utf16_buffer_type replacement;
        replacement.reserve(256);
        buf.swap(replacement);
    }
    return hash_result;
}
}  // namespace qlog::utility
