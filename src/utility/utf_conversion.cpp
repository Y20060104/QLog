#include "qlog/utility/utf_conversion.hpp"

#include <cassert>
#include <cstddef>
#include <cstring>

namespace qlog::utility {
std::uint32_t utf16_to_utf8_sw(const char* src, std::uint32_t src_byte_len, char* dst,
                               std::uint32_t dst_capacity) {
    static_assert(sizeof(char16_t) == 2);

    const std::uint32_t unit_count = src_byte_len / 2U;

    assert(unit_count <= dst_capacity / 3U);
    (void)dst_capacity;

    if (unit_count == 0) {
        return 0;
    }

    assert(src != nullptr);
    assert(dst != nullptr);

    const auto read_unit = [src](std::uint32_t index) {
        char16_t unit{};
        std::memcpy(&unit, src + static_cast<std::size_t>(index) * sizeof(unit), sizeof(unit));

        return static_cast<std::uint32_t>(unit);
    };

    std::uint32_t input = 0;
    std::uint32_t output = 0;

    while (input < unit_count) {
        std::uint32_t c = read_unit(input);

        ++input;

        if (c < 0x80U) {
            dst[output++] = static_cast<char>(c);

            while (input < unit_count) {
                const std::uint32_t next = read_unit(input);
                if (next >= 0x80U) {
                    break;
                }

                dst[output++] = static_cast<char>(next);
                ++input;
            }
        } else if (c < 0x800U) {
            dst[output++] = static_cast<char>(static_cast<unsigned char>(0xC0U | (c >> 6)));

            dst[output++] = static_cast<char>(static_cast<unsigned char>(0x80U | (c & 0x3FU)));
        } else if (c >= 0xD800U && c <= 0xDFFFU) {
            if (c >= 0xDC00U) {
                continue;
            }

            if (input < unit_count) {
                const std::uint32_t low = read_unit(input);

                if (low >= 0xDC00U && low <= 0xDFFFU) {
                    ++input;

                    c = 0x10000U + ((c & 0x3FFU) << 10) + (low & 0x3FFU);

                    dst[output++] =
                        static_cast<char>(static_cast<unsigned char>(0xF0U | (c >> 18)));

                    dst[output++] =
                        static_cast<char>(static_cast<unsigned char>(0x80U | ((c >> 12) & 0x3FU)));

                    dst[output++] =
                        static_cast<char>(static_cast<unsigned char>(0x80U | ((c >> 6) & 0x3FU)));

                    dst[output++] =
                        static_cast<char>(static_cast<unsigned char>(0x80U | (c & 0x3FU)));
                    // CR: 为什么一次6位？
                }
            }
        } else {
            dst[output++] = static_cast<char>(static_cast<unsigned char>(0xE0U | (c >> 12)));

            dst[output++] =
                static_cast<char>(static_cast<unsigned char>(0x80U | ((c >> 6) & 0x3FU)));

            dst[output++] = static_cast<char>(static_cast<unsigned char>(0x80U | (c & 0x3FU)));
        }
    }
    return output;
}
}  // namespace qlog::utility