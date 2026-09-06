#pragma once

#include <cstdint>
#include <type_traits>

namespace qlog::detail {

enum class ArgumentTag : std::uint8_t {
    Invalid = 0x00,
    Bool = 0x01,
    Char = 0x02,
    Int8 = 0x03,
    UInt8 = 0x04,
    Int16 = 0x05,
    UInt16 = 0x06,
    Int32 = 0x07,
    UInt32 = 0x08,
    Int64 = 0x09,
    UInt64 = 0x0A,
    F32 = 0x0B,
    F64 = 0x0C,
    Pointer64 = 0x0D,
    Utf8String = 0x0E,
    NullUtf8 = 0x0F,
};

static_assert(std::is_same_v<std::underlying_type_t<ArgumentTag>, std::uint8_t>);
static_assert(sizeof(ArgumentTag) == 1U);

}  // namespace qlog::detail
