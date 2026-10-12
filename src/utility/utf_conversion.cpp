#include "qlog/utility/utf_conversion.hpp"

#include <cassert>

namespace qlog::utility {
std::uint32_t utf16_to_utf8_sw(const char16_t* src, std::uint32_t src_character_num, char* dst,
                               std::uint32_t dst_character_num) {
    static_assert(sizeof(char16_t) == 2);
    (void)dst_character_num;

    if (src_character_num == 0) {
        return 0;
    }
    assert(src != nullptr);
    assert(dst != nullptr);

    const char16_t* src_end = src + src_character_num;
    char* dst_ptr = dst;

    while (src < src_end) {
        std::uint32_t c = *src++;

        if (c < 0x80U) {
            *dst_ptr++ = static_cast<char>(c);
            while (src < src_end && *src < 0x80U) {
                *dst_ptr++ = static_cast<char>(*src++);
            }
        } else if (c < 0x800U) {
            *dst_ptr++ = static_cast<char>(static_cast<unsigned char>(0xC0U | (c >> 6)));
            *dst_ptr++ = static_cast<char>(static_cast<unsigned char>(0x80U | (c & 0x3FU)));
        } else if (c >= 0xD800U && c <= 0xDFFFU) {
            if (c >= 0xDC00U) {
                continue;
            }
            if (src < src_end) {
                const std::uint32_t c2 = *src;
                if (c2 >= 0xDC00U && c2 <= 0xDFFFU) {
                    ++src;
                    c = (((c & 0x3FFU) << 10) | (c2 & 0x3FFU)) + 0x10000U;

                    *dst_ptr++ = static_cast<char>(static_cast<unsigned char>(0xF0U | (c >> 18)));
                    *dst_ptr++ =
                        static_cast<char>(static_cast<unsigned char>(0x80U | ((c >> 12) & 0x3FU)));
                    *dst_ptr++ =
                        static_cast<char>(static_cast<unsigned char>(0x80U | ((c >> 6) & 0x3FU)));
                    *dst_ptr++ = static_cast<char>(static_cast<unsigned char>(0x80U | (c & 0x3FU)));
                    // CR: 为什么一次6位？
                }
            }
        } else {
            *dst_ptr++ = static_cast<char>(static_cast<unsigned char>(0xE0U | (c >> 12)));
            *dst_ptr++ = static_cast<char>(static_cast<unsigned char>(0x80U | ((c >> 6) & 0x3FU)));
            *dst_ptr++ = static_cast<char>(static_cast<unsigned char>(0x80U | (c & 0x3FU)));
        }
    }
    return static_cast<std::uint32_t>(dst_ptr - dst);
}

std::uint32_t utf8_to_utf16_sw(const char* src, std::uint32_t src_character_num, char16_t* dst,
                               std::uint32_t dst_character_num) {
    static_assert(sizeof(char16_t) == 2);
    (void)dst_character_num;

    if (src_character_num == 0) {
        return 0;
    }

    assert(src != nullptr);
    assert(dst != nullptr);

    const auto* s = reinterpret_cast<const std::uint8_t*>(src);
    const auto* end = s + src_character_num;
    char16_t* d = dst;

    while (s < end) {
        const std::uint8_t c = *s++;

        if (c < 0x80U) {
            *d++ = static_cast<char16_t>(c);
            while (s < end && *s < 0x80U) {
                *d++ = static_cast<char16_t>(*s++);
            }

        } else if ((c & 0xE0U) == 0xC0U) {
            *d++ = static_cast<char16_t>((c & 0x1FU) << 6) | (*s++ & 0x3FU);
        } else if ((c & 0xF0U) == 0xE0U) {
            *d++ =
                static_cast<char16_t>((c & 0x0FU) << 12) | ((s[0] & 0x3FU) << 6 | (s[1] & 0x3FU));
            s += 2;
        } else {
            std::uint32_t cp = ((c & 0x07U) << 18) | ((s[0] & 0x3FU) << 12) |
                               ((s[1] & 0x3FU) << 6) | (s[2] & 0x3FU);
            cp -= 0x10000U;

            *d++ = static_cast<char16_t>((cp >> 10) + 0xD800U);
            *d++ = static_cast<char16_t>((cp & 0x3FFU) + 0xDC00U);
            s += 3;
        }
    }
    return static_cast<std::uint32_t>(d - dst);
}
}  // namespace qlog::utility