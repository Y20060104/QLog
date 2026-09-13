#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <limits>
#include <qlog/detail/checked_size.hpp>
#include <qlog/detail/format_hash.hpp>
#include <qlog/detail/format_hash_reference.hpp>
#include <qlog/detail/record_measure.hpp>

#include "format_hash_test_access.hpp"
namespace {
using namespace qlog::detail;
TEST(RecordArithmetic, CheckedAddExactMaximumAndOverflowPreservesOutput) {
    const auto max = std::numeric_limits<std::size_t>::max();
    std::size_t out = 19;
    EXPECT_TRUE(checked_add(max - 1, 1, out));
    EXPECT_EQ(out, max);
    out = 19;
    EXPECT_FALSE(checked_add(max, 1, out));
    EXPECT_EQ(out, 19U);
    EXPECT_TRUE(checked_add(0, 0, out));
    EXPECT_EQ(out, 0U);
}
TEST(RecordArithmetic, CheckedMultiplyZeroExactMaximumAndOverflowPreservesOutput) {
    const auto max = std::numeric_limits<std::size_t>::max();
    std::size_t out = 19;
    EXPECT_TRUE(checked_mul(0, max, out));
    EXPECT_EQ(out, 0U);
    EXPECT_TRUE(checked_mul(max, 1, out));
    EXPECT_EQ(out, max);
    out = 19;
    EXPECT_FALSE(checked_mul(max, 2, out));
    EXPECT_EQ(out, 19U);
}
TEST(MeasureBoundary, argument_length_out_of_range) {
    NormalizedArgument a{};
    MeasureFailure f{};
    std::byte byte{};
    EXPECT_FALSE(record_measure_impl::store_utf8(&byte, std::size_t{UINT32_MAX} + 1U, 3, a, f));
    EXPECT_EQ(f.error, MeasureError::argument_length_out_of_range);
    EXPECT_EQ(f.argument_index, 3U);
}
TEST(MeasureBoundary, invalid_string_metadata) {
    NormalizedArgument a{};
    MeasureFailure f{};
    EXPECT_FALSE(record_measure_impl::store_utf8(nullptr, 1, 2, a, f));
    EXPECT_EQ(f.error, MeasureError::invalid_string_metadata);
    EXPECT_EQ(f.argument_index, 2U);
}
TEST(MeasureBoundary, args_length_out_of_range) {
    NormalizedArgument a{};
    a.tag = ArgumentTag::Utf8String;
    a.byte_count = UINT32_MAX;
    std::size_t args = 0, payload = 0;
    MeasureFailure f{};
    EXPECT_FALSE(record_measure_impl::measure_sizes(&a, 1, 0, UINT32_MAX, args, payload, f));
    EXPECT_EQ(f.error, MeasureError::args_length_out_of_range);
}
TEST(MeasureBoundary, record_length_out_of_range) {
    NormalizedArgument a{};
    a.tag = ArgumentTag::Utf8String;
    a.byte_count = UINT32_MAX - 5U;
    std::size_t args = 0, payload = 0;
    MeasureFailure f{};
    EXPECT_FALSE(record_measure_impl::measure_sizes(&a, 1, 0, UINT32_MAX, args, payload, f));
    EXPECT_EQ(f.error, MeasureError::record_length_out_of_range);
}
TEST(MeasureBoundary, size_overflow) {
    const auto max = std::numeric_limits<std::size_t>::max();
    std::size_t args = 0, payload = 0;
    MeasureFailure f{};
    EXPECT_FALSE(record_measure_impl::measure_sizes(nullptr, 0, max, UINT32_MAX, args, payload, f));
    EXPECT_EQ(f.error, MeasureError::size_overflow);
    NormalizedArgument a{};
    a.tag = ArgumentTag::UInt64;
    EXPECT_FALSE(
        record_measure_impl::measure_sizes(&a, 1, max - 32U, UINT32_MAX, args, payload, f));
    EXPECT_EQ(f.error, MeasureError::size_overflow);
}
TEST(MeasureBoundary, invalid_limits) {
    const auto m = measure_record({}, 31U);
    ASSERT_FALSE(m.succeeded());
    EXPECT_EQ(m.failure()->error, MeasureError::invalid_limits);
}
TEST(MeasureBoundary, invalid_format_metadata) {
    const auto m = measure_record({nullptr, 1, 0}, 4096U);
    ASSERT_FALSE(m.succeeded());
    EXPECT_EQ(m.failure()->error, MeasureError::invalid_format_metadata);
}
TEST(MeasureBoundary, format_too_large) {
    std::array<std::byte, 8193> b{};
    const auto m = measure_record({b.data(), b.size(), 0}, UINT32_MAX);
    ASSERT_FALSE(m.succeeded());
    EXPECT_EQ(m.failure()->error, MeasureError::format_too_large);
}
TEST(MeasureBoundary, payload_too_large) {
    const auto m = measure_record({}, 32U, 1);
    ASSERT_FALSE(m.succeeded());
    EXPECT_EQ(m.failure()->error, MeasureError::payload_too_large);
}
TEST(ArgumentMapping, RuntimeMappingAndNullPointerWrapper) {
    for (unsigned k = 0; k <= static_cast<unsigned>(ArgumentKind::CStr); ++k) {
        const auto kind = static_cast<ArgumentKind>(k);
        const auto tag = wire_tag_for(kind);
        if (kind == ArgumentKind::Unsupported || kind == ArgumentKind::CStr)
            EXPECT_EQ(tag, ArgumentTag::Invalid);
        else
            EXPECT_NE(tag, ArgumentTag::Invalid);
    }
    EXPECT_EQ(wire_tag_for(static_cast<ArgumentKind>(255)), ArgumentTag::Invalid);
    const auto p = qlog::ptr(nullptr);
    EXPECT_EQ(p.value, 0U);
}
}  // namespace

namespace {
TEST(HashReference, RuntimeCharAndChar8ArraysMatchRawReference) {
    char text[] = {'a', char(0x80), 0, 'b', 0};
    char8_t utf8[] = {u8'a', char8_t(0x80), 0, u8'b', 0};
    const auto expected = qlog::detail::format_hash_reference::hash_raw_ref(text, 4);
    EXPECT_EQ(qlog::detail::hash_literal_stored(text),
              qlog::detail::stored_hash_from_raw(expected));
    EXPECT_EQ(qlog::detail::hash_literal_stored(utf8),
              qlog::detail::stored_hash_from_raw(expected));
}
}  // namespace

namespace {
TEST(HashReference, RuntimeEmptyLiteralUsesStoredZeroNormalization) {
    char empty[] = {0};
    char8_t empty_utf8[] = {0};
    using EmptyHash = std::uint64_t (*)(const char(&)[1]) noexcept;
    EmptyHash volatile runtime_hash = &qlog::detail::hash_literal_stored<char, 1>;
    EXPECT_EQ(runtime_hash(empty), 1U);
    EXPECT_EQ(qlog::detail::hash_literal_stored(empty_utf8), 1U);
}
}  // namespace

namespace {
TEST(HashReference, NonemptyFrozenRawZeroVectorNormalizesToOne) {
    // Independently solved over GF(2) with the bitwise CRC32C polynomial, not produced by QLog.
    const std::array<std::uint8_t, 32> bytes{0, 0,    0,    0,    0xD4, 0x3A, 0x70, 0x58, 0, 0, 0,
                                             0, 0x92, 0x3B, 0xA1, 0x5B, 0,    0,    0,    0, 0, 0,
                                             0, 0,    0,    0,    0,    0,    0,    0,    0, 0};
    char text[33]{};
    std::memcpy(text, bytes.data(), bytes.size());
    EXPECT_EQ(qlog::detail::format_hash_reference::hash_raw_ref(text, 32), 0U);
    using LiteralHash = std::uint64_t (*)(const char(&)[33]) noexcept;
    LiteralHash volatile runtime_hash = &qlog::detail::hash_literal_stored<char, 33>;
    EXPECT_EQ(runtime_hash(text), 1U);
    const auto* source = reinterpret_cast<const std::byte*>(bytes.data());
    const auto verify = [&](const qlog::detail::FormatHashDispatch& dispatch) {
        EXPECT_EQ(dispatch.hash_raw_unchecked(source, bytes.size()), 0U);
        const auto result = qlog::detail::hash_format_stored(dispatch, source, bytes.size());
        ASSERT_TRUE(result.succeeded());
        EXPECT_EQ(*result.stored_hash(), 1U);
    };
    verify(qlog::detail::FormatHashTestAccess::software());
    verify(qlog::detail::FormatHashDispatch::automatic());
    if (auto hardware = qlog::detail::FormatHashTestAccess::hardware()) verify(*hardware);
}
}  // namespace
