#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <qlog/detail/argument_tag.hpp>
#include <type_traits>

namespace {

using qlog::detail::ArgumentTag;

constexpr std::array kV1Tags{
    ArgumentTag::Invalid, ArgumentTag::Bool,      ArgumentTag::Char,       ArgumentTag::Int8,
    ArgumentTag::UInt8,   ArgumentTag::Int16,     ArgumentTag::UInt16,     ArgumentTag::Int32,
    ArgumentTag::UInt32,  ArgumentTag::Int64,     ArgumentTag::UInt64,     ArgumentTag::F32,
    ArgumentTag::F64,     ArgumentTag::Pointer64, ArgumentTag::Utf8String, ArgumentTag::NullUtf8,
};

static_assert(std::is_enum_v<ArgumentTag>);
static_assert(std::is_same_v<std::underlying_type_t<ArgumentTag>, std::uint8_t>);
static_assert(sizeof(ArgumentTag) == 1U);
static_assert(kV1Tags.size() == 16U);

TEST(ArgumentTagAbi, UsesFrozenContiguousWireValues) {
    for (std::size_t index = 0; index < kV1Tags.size(); ++index) {
        EXPECT_EQ(static_cast<std::uint8_t>(kV1Tags[index]), index)
            << "unexpected V1 wire value at tag index " << index;
    }
}

TEST(ArgumentTagAbi, KeepsReservedBoundariesDistinct) {
    EXPECT_EQ(static_cast<std::uint8_t>(ArgumentTag::Invalid), 0x00U);
    EXPECT_EQ(static_cast<std::uint8_t>(ArgumentTag::NullUtf8), 0x0FU);
    EXPECT_LT(static_cast<std::uint8_t>(ArgumentTag::NullUtf8), 0x10U);
}

}  // namespace
