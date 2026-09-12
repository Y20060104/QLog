#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <qlog/arguments.hpp>
#include <qlog/detail/format_hash_reference.hpp>
#include <qlog/detail/record_encoder.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "format_hash_test_access.hpp"

namespace {
using namespace qlog::detail;
const RecordValidationPolicy kAllLevels{{~0ULL, ~0ULL, ~0ULL, ~0ULL}, true};

void append_le(std::vector<std::byte>& out, std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i)
        out.push_back(static_cast<std::byte>((value >> (8U * i)) & 255U));
}

std::vector<std::byte> golden(std::string_view format, std::uint64_t hash,
                              const RecordMetadata& metadata, std::uint16_t count,
                              const std::vector<std::byte>& args) {
    // Explicit wire offsets, not a memcpy of the production Header or encoder output.
    std::vector<std::byte> out;
    append_le(out, metadata.time_value, 8U);
    append_le(out, hash, 8U);
    append_le(out, format.size(), 4U);
    append_le(out, args.size(), 4U);
    append_le(out, metadata.category_id, 4U);
    append_le(out, count, 2U);
    append_le(out, metadata.level, 1U);
    append_le(out, metadata.flags, 1U);
    for (char c : format) out.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    out.insert(out.end(), args.begin(), args.end());
    return out;
}

std::vector<std::byte> bytes(std::initializer_list<unsigned> values) {
    std::vector<std::byte> out;
    for (auto value : values) out.push_back(static_cast<std::byte>(value));
    return out;
}

TEST(RecordEncoder, EveryTagMatchesIndependentGoldenAtAllDestinationOffsets) {
    constexpr std::string_view format = "abc";
    const RecordMetadata metadata{0x0102030405060708ULL, 0xA1B2C3D4U, 200U, 1U};
    const auto measured = measure_record(
        {reinterpret_cast<const std::byte*>(format.data()), format.size(), 0U}, 4096U, true, 'A',
        std::int8_t{-2}, std::uint8_t{0xAB}, std::int16_t{-2}, std::uint16_t{0x1234},
        std::int32_t{-2}, std::uint32_t{0x12345678}, std::int64_t{-2},
        std::uint64_t{0x0102030405060708ULL}, 1.0F, -0.0, nullptr, "a\0b", "", qlog::cstr(nullptr));
    ASSERT_TRUE(measured.succeeded());
    const auto args =
        bytes({0x01, 0x01, 0x02, 0x41, 0x03, 0xFE, 0x04, 0xAB, 0x05, 0xFE, 0xFF, 0x06, 0x34, 0x12,
               0x07, 0xFE, 0xFF, 0xFF, 0xFF, 0x08, 0x78, 0x56, 0x34, 0x12, 0x09, 0xFE, 0xFF, 0xFF,
               0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x0A, 0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
               0x0B, 0x00, 0x00, 0x80, 0x3F, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80,
               0x0D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0E, 0x03, 0x00, 0x00, 0x00,
               0x61, 0x00, 0x62, 0x0E, 0x00, 0x00, 0x00, 0x00, 0x0F});
    const auto expected = golden(format, 0x33bbc033d64581afULL, metadata, 16U, args);
    ASSERT_EQ(measured.prepared()->payload_size(), expected.size());
    for (auto dispatch : {FormatHashTestAccess::software(), FormatHashDispatch::automatic()}) {
        for (std::size_t offset = 0; offset < 32U; ++offset) {
            std::vector<std::byte> destination(expected.size() + 96U, std::byte{0xA5});
            auto* start = destination.data() + 32U + offset;
            const auto result = encode_v1(start, expected.size(), *measured.prepared(), metadata,
                                          kAllLevels, dispatch);
            ASSERT_TRUE(result.succeeded());
            EXPECT_EQ(result.failure(), nullptr);
            EXPECT_EQ(result.bytes_written(), expected.size());
            EXPECT_TRUE(std::equal(expected.begin(), expected.end(), start));
            EXPECT_TRUE(std::all_of(destination.data(), start,
                                    [](auto b) { return b == std::byte{0xA5}; }));
            EXPECT_TRUE(std::all_of(start + expected.size(),
                                    destination.data() + destination.size(),
                                    [](auto b) { return b == std::byte{0xA5}; }));
        }
    }
}

unsigned hash_calls = 0;
unsigned copy_calls = 0;
std::size_t copied_size = 0;
std::uint64_t spy_hash(const std::byte*, std::size_t) noexcept {
    ++hash_calls;
    return 0U;
}
std::uint64_t spy_copy(const std::byte* src, std::byte* dst, std::size_t size) noexcept {
    ++copy_calls;
    copied_size = size;
    if (size != 0U) std::memcpy(dst, src, size);
    return 0U;
}
void reset_spy() {
    hash_calls = copy_calls = 0U;
    copied_size = 0U;
}

TEST(RecordEncoder, EmptyRecordAndPrecomputedOrRuntimeHashRouting) {
    const auto dispatch = FormatHashTestAccess::custom(spy_hash, spy_copy);
    const RecordMetadata metadata{};
    for (auto format : {std::string_view{}, std::string_view{"abc"}}) {
        for (bool use_precomputed : {false, true}) {
            const std::uint64_t precomputed =
                !use_precomputed ? 0U : (format.empty() ? 1U : 0x33bbc033d64581afULL);
            reset_spy();
            const auto measured = measure_record(
                {reinterpret_cast<const std::byte*>(format.data()), format.size(), precomputed},
                4096U);
            ASSERT_TRUE(measured.succeeded());
            std::vector<std::byte> output(measured.prepared()->payload_size(), std::byte{0xA5});
            const auto result = encode_v1(output.data(), output.size(), *measured.prepared(),
                                          metadata, kAllLevels, dispatch);
            ASSERT_TRUE(result.succeeded());
            EXPECT_EQ(output,
                      golden(format, precomputed != 0U ? precomputed : 1U, metadata, 0U, {}));
            EXPECT_EQ(hash_calls, 0U);
            EXPECT_EQ(copy_calls, precomputed == 0U && !format.empty() ? 1U : 0U);
            EXPECT_EQ(copied_size, copy_calls != 0U ? format.size() : 0U);
        }
    }
}

TEST(RecordEncoder, EveryRecoverableErrorPreservesEntireDestinationAndSkipsHash) {
    const auto measured = measure_record({}, 4096U, "test");
    ASSERT_TRUE(measured.succeeded());
    const auto& prepared = *measured.prepared();
    const auto dispatch = FormatHashTestAccess::custom(spy_hash, spy_copy);
    const RecordValidationPolicy denied{{0U, 0U, 0U, 0U}, false};
    const RecordValidationPolicy no_fallback{{~0ULL, ~0ULL, ~0ULL, ~0ULL}, false};
    std::vector<std::byte> destination(prepared.payload_size() + 64U, std::byte{0xCD});
    const auto before = destination;
    auto* dst = destination.data() + 32U;
    const auto check = [&](std::byte* target, std::size_t size, RecordMetadata metadata,
                           const RecordValidationPolicy& policy, EncodeError expected) {
        reset_spy();
        const auto result = encode_v1(target, size, prepared, metadata, policy, dispatch);
        EXPECT_FALSE(result.succeeded());
        EXPECT_EQ(result.bytes_written(), 0U);
        ASSERT_NE(result.failure(), nullptr);
        EXPECT_EQ(*result.failure(), expected);
        EXPECT_EQ(destination, before);
        EXPECT_EQ(hash_calls, 0U);
        EXPECT_EQ(copy_calls, 0U);
    };
    const auto size = prepared.payload_size();
    check(nullptr, size, {1U, 0U, 0U, 0xFFU}, denied, EncodeError::invalid_destination_metadata);
    check(nullptr, 0U, {}, kAllLevels, EncodeError::destination_size_mismatch);
    check(dst, size - 1U, {1U, 0U, 0U, 0xFFU}, denied, EncodeError::destination_size_mismatch);
    check(dst, size + 1U, {}, kAllLevels, EncodeError::destination_size_mismatch);
    check(dst, size, {1U, 0U, 0U, 0xFFU}, denied, EncodeError::invalid_level);
    check(dst, size, {1U, 0U, 0U, 0xFFU}, kAllLevels, EncodeError::unknown_flags);
    check(dst, size, {1U, 0U, 0U, 3U}, no_fallback, EncodeError::reserved_timestamp_status);
    check(dst, size, {1U, 0U, 0U, 2U}, no_fallback, EncodeError::invalid_time_value);
    check(dst, size, {1U, 0U, 0U, 1U}, no_fallback, EncodeError::fallback_timestamp_not_configured);
}

template <std::size_t... I>
auto measure_max_args(std::index_sequence<I...>) {
    return measure_record({}, 4096U, (static_cast<void>(I), true)...);
}

TEST(RecordEncoder, MaximumArgumentCountAndScalarSnapshot) {
    const auto measured = measure_max_args(std::make_index_sequence<32U>{});
    ASSERT_TRUE(measured.succeeded());
    std::vector<std::byte> args;
    for (unsigned i = 0; i < 32U; ++i) {
        args.push_back(std::byte{1});
        args.push_back(std::byte{1});
    }
    std::vector<std::byte> output(measured.prepared()->payload_size());
    const auto dispatch = FormatHashTestAccess::software();
    ASSERT_TRUE(
        encode_v1(output.data(), output.size(), *measured.prepared(), {}, kAllLevels, dispatch)
            .succeeded());
    EXPECT_EQ(output, golden({}, 1U, {}, 32U, args));
    std::int32_t scalar = -2;
    const auto snapshot = measure_record({}, 4096U, scalar);
    scalar = 99;
    ASSERT_TRUE(snapshot.succeeded());
    output.resize(snapshot.prepared()->payload_size());
    ASSERT_TRUE(
        encode_v1(output.data(), output.size(), *snapshot.prepared(), {}, kAllLevels, dispatch)
            .succeeded());
    EXPECT_EQ(output, golden({}, 1U, {}, 1U, bytes({0x07, 0xFE, 0xFF, 0xFF, 0xFF})));
}

TEST(RecordEncoder, FloatingPointPayloadBitsAndUtf8ViewsArePreserved) {
    const auto f = std::bit_cast<float>(std::uint32_t{0x7FC01234U});
    const auto d = std::bit_cast<double>(std::uint64_t{0x7FF8000000005678ULL});
    const auto measured = measure_record({}, 4096U, f, d, std::u8string_view{u8"\u4e2d"});
    ASSERT_TRUE(measured.succeeded());
    std::vector<std::byte> output(measured.prepared()->payload_size());
    ASSERT_TRUE(encode_v1(output.data(), output.size(), *measured.prepared(), {}, kAllLevels,
                          FormatHashTestAccess::software())
                    .succeeded());
    EXPECT_EQ(output, golden({}, 1U, {}, 3U, bytes({0x0B, 0x34, 0x12, 0xC0, 0x7F, 0x0C, 0x78, 0x56,
                                                    0x00, 0x00, 0x00, 0x00, 0xF8, 0x7F, 0x0E, 0x03,
                                                    0x00, 0x00, 0x00, 0xE4, 0xB8, 0xAD})));
}

TEST(RecordEncoder, LongStringsAndMaximumFormatUseExactLengths) {
    const std::string format(8192U, 'x');
    const std::string text(65537U, 'y');
    const auto hash =
        stored_hash_from_raw(format_hash_reference::hash_raw_ref(format.data(), format.size()));
    // Precompute from the same input before testing the encoder's exact copies.
    const auto measured = measure_record(
        {reinterpret_cast<const std::byte*>(format.data()), format.size(), hash}, 100000U, text);
    ASSERT_TRUE(measured.succeeded());
    std::vector<std::byte> output(measured.prepared()->payload_size() + 1U, std::byte{0xCC});
    ASSERT_TRUE(encode_v1(output.data(), output.size() - 1U, *measured.prepared(), {}, kAllLevels,
                          FormatHashTestAccess::software())
                    .succeeded());
    auto args = bytes({0x0E, 0x01, 0x00, 0x01, 0x00});
    args.insert(args.end(), text.size(), std::byte{'y'});
    auto expected = golden(format, hash, {}, 1U, args);
    expected.push_back(std::byte{0xCC});
    EXPECT_EQ(output, expected);
}
}  // namespace
