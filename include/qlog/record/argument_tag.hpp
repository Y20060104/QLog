#pragma once

#include <cstdint>
#include <type_traits>

namespace qlog::record {

enum class ArgumentTag : std::uint8_t {
    unsupported_type = 0,
    null_type = 1,
    pointer_type = 2,
    bool_type = 3,
    char_type = 4,
    char16_type = 5,
    char32_type = 6,
    int8_type = 7,
    uint8_type = 8,
    int16_type = 9,
    uint16_type = 10,
    int32_type = 11,
    uint32_type = 12,
    int64_type = 13,
    uint64_type = 14,
    float_type = 15,
    double_type = 16,
    string_utf8_type = 17,
    string_utf16_type = 18,
    string_utf32_type = 19,
    string_utf_mixed_type = 20,
};

static_assert(std::is_same_v<std::underlying_type_t<ArgumentTag>, std::uint8_t>);
static_assert(sizeof(ArgumentTag) == 1U);

}  // namespace qlog::record
