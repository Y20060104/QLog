#include <gtest/gtest.h>

#include <array>
#include <limits>
#include <qlog/detail/checked_size.hpp>
#include <qlog/detail/format_hash.hpp>
#include <qlog/detail/format_hash_reference.hpp>
#include <qlog/detail/record_measure.hpp>
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
