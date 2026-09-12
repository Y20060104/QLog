#include <gtest/gtest.h>

#include <array>
#include <qlog/detail/record_decoder.hpp>
#include <qlog/detail/record_encoder.hpp>
#include <vector>
namespace {
using namespace qlog::detail;
const RecordValidationPolicy strict{{1, 0, 0, 0}, false};
void expect_decode(std::vector<std::byte> b, DecodeError error, std::size_t offset,
                   std::uint8_t index = 255) {
    std::array<DecodedArg, 32> w{};
    const auto result = decode_v1(b.data(), b.size(), w.data(), 32, strict);
    ASSERT_FALSE(result.succeeded());
    ASSERT_NE(result.failure(), nullptr);
    EXPECT_EQ(result.failure()->error, error);
    EXPECT_EQ(result.failure()->error_offset, offset);
    EXPECT_EQ(result.failure()->argument_index, index);
}
TEST(DecodeErrors, record_too_small) {
    std::vector<std::byte> b(32);
    b.resize(31);
    expect_decode(b, DecodeError::record_too_small, 31, 255);
}
TEST(DecodeErrors, invalid_level) {
    std::vector<std::byte> b(32);
    b[30] = std::byte{1};
    expect_decode(b, DecodeError::invalid_level, 30, 255);
}
TEST(DecodeErrors, unknown_flags) {
    std::vector<std::byte> b(32);
    b[31] = std::byte{4};
    expect_decode(b, DecodeError::unknown_flags, 31, 255);
}
TEST(DecodeErrors, reserved_timestamp_status) {
    std::vector<std::byte> b(32);
    b[31] = std::byte{3};
    expect_decode(b, DecodeError::reserved_timestamp_status, 31, 255);
}
TEST(DecodeErrors, invalid_time_value) {
    std::vector<std::byte> b(32);
    b[31] = std::byte{2};
    b[0] = std::byte{1};
    expect_decode(b, DecodeError::invalid_time_value, 0, 255);
}
TEST(DecodeErrors, fallback_timestamp_not_configured) {
    std::vector<std::byte> b(32);
    b[31] = std::byte{1};
    expect_decode(b, DecodeError::fallback_timestamp_not_configured, 31, 255);
}
TEST(DecodeErrors, format_too_large) {
    std::vector<std::byte> b(32);
    b[16] = std::byte{1};
    b[17] = std::byte{32};
    expect_decode(b, DecodeError::format_too_large, 16, 255);
}
TEST(DecodeErrors, invalid_format_length) {
    std::vector<std::byte> b(32);
    b[16] = std::byte{1};
    expect_decode(b, DecodeError::invalid_format_length, 16, 255);
}
TEST(DecodeErrors, invalid_args_length) {
    std::vector<std::byte> b(32);
    b[20] = std::byte{1};
    expect_decode(b, DecodeError::invalid_args_length, 20, 255);
}
TEST(DecodeErrors, arg_count_exceeded) {
    std::vector<std::byte> b(32);
    b[28] = std::byte{33};
    expect_decode(b, DecodeError::arg_count_exceeded, 28, 255);
}
TEST(DecodeErrors, invalid_or_unknown_tag) {
    std::vector<std::byte> b(32);
    b.push_back(std::byte{0});
    b[20] = std::byte{1};
    b[28] = std::byte{1};
    expect_decode(b, DecodeError::invalid_or_unknown_tag, 32, 0);
}
TEST(DecodeErrors, invalid_bool) {
    std::vector<std::byte> b(32);
    b.push_back(std::byte{1});
    b.push_back(std::byte{2});
    b[20] = std::byte{2};
    b[28] = std::byte{1};
    expect_decode(b, DecodeError::invalid_bool, 33, 0);
}
TEST(DecodeErrors, truncated_value) {
    std::vector<std::byte> b(32);
    b.push_back(std::byte{9});
    b[20] = std::byte{1};
    b[28] = std::byte{1};
    expect_decode(b, DecodeError::truncated_value, 33, 0);
}
TEST(DecodeErrors, truncated_string_length) {
    std::vector<std::byte> b(32);
    b.push_back(std::byte{14});
    b[20] = std::byte{1};
    b[28] = std::byte{1};
    expect_decode(b, DecodeError::truncated_string_length, 33, 0);
}
TEST(DecodeErrors, truncated_string) {
    std::vector<std::byte> b(32);
    b.resize(37);
    b[32] = std::byte{14};
    b[33] = std::byte{1};
    b[20] = std::byte{5};
    b[28] = std::byte{1};
    expect_decode(b, DecodeError::truncated_string, 37, 0);
}
TEST(DecodeErrors, decoded_count_mismatch) {
    std::vector<std::byte> b(32);
    b[28] = std::byte{1};
    expect_decode(b, DecodeError::decoded_count_mismatch, 32, 0);
}
TEST(DecodeErrors, trailing_args_bytes) {
    std::vector<std::byte> b(32);
    b.push_back(std::byte{15});
    b[20] = std::byte{1};
    expect_decode(b, DecodeError::trailing_args_bytes, 32, 0);
}
TEST(DecodeErrors, invalid_payload_metadata) {
    std::array<DecodedArg, 32> w{};
    std::array<std::byte, 32> b{};
    const auto result = decode_v1(nullptr, 1, w.data(), 32, strict);
    (void)w;
    (void)b;
    ASSERT_FALSE(result.succeeded());
    EXPECT_EQ(result.failure()->error, DecodeError::invalid_payload_metadata);
    EXPECT_EQ(result.failure()->error_offset, 0U);
    EXPECT_EQ(result.failure()->argument_index, 255U);
}
TEST(DecodeErrors, invalid_workspace) {
    std::array<DecodedArg, 32> w{};
    std::array<std::byte, 32> b{};
    const auto result = decode_v1(b.data(), b.size(), nullptr, 32, strict);
    (void)w;
    (void)b;
    ASSERT_FALSE(result.succeeded());
    EXPECT_EQ(result.failure()->error, DecodeError::invalid_workspace);
    EXPECT_EQ(result.failure()->error_offset, 0U);
    EXPECT_EQ(result.failure()->argument_index, 255U);
}
TEST(EncodeErrors, invalid_destination_metadata) {
    const auto m = measure_record({}, 4096U);
    ASSERT_TRUE(m.succeeded());
    std::array<std::byte, 32> out;
    out.fill(std::byte{0xA5});
    const auto before = out;
    RecordMetadata metadata{};
    const auto result =
        encode_v1(nullptr, 32, *m.prepared(), metadata, strict, FormatHashDispatch::automatic());
    ASSERT_FALSE(result.succeeded());
    EXPECT_EQ(*result.failure(), EncodeError::invalid_destination_metadata);
    EXPECT_EQ(result.bytes_written(), 0U);
    EXPECT_EQ(out, before);
}
TEST(EncodeErrors, destination_size_mismatch) {
    const auto m = measure_record({}, 4096U);
    ASSERT_TRUE(m.succeeded());
    std::array<std::byte, 32> out;
    out.fill(std::byte{0xA5});
    const auto before = out;
    RecordMetadata metadata{};
    const auto result =
        encode_v1(out.data(), 31, *m.prepared(), metadata, strict, FormatHashDispatch::automatic());
    ASSERT_FALSE(result.succeeded());
    EXPECT_EQ(*result.failure(), EncodeError::destination_size_mismatch);
    EXPECT_EQ(result.bytes_written(), 0U);
    EXPECT_EQ(out, before);
}
TEST(EncodeErrors, invalid_level) {
    const auto m = measure_record({}, 4096U);
    ASSERT_TRUE(m.succeeded());
    std::array<std::byte, 32> out;
    out.fill(std::byte{0xA5});
    const auto before = out;
    RecordMetadata metadata{};
    metadata.level = 1;
    const auto result =
        encode_v1(out.data(), 32, *m.prepared(), metadata, strict, FormatHashDispatch::automatic());
    ASSERT_FALSE(result.succeeded());
    EXPECT_EQ(*result.failure(), EncodeError::invalid_level);
    EXPECT_EQ(result.bytes_written(), 0U);
    EXPECT_EQ(out, before);
}
TEST(EncodeErrors, unknown_flags) {
    const auto m = measure_record({}, 4096U);
    ASSERT_TRUE(m.succeeded());
    std::array<std::byte, 32> out;
    out.fill(std::byte{0xA5});
    const auto before = out;
    RecordMetadata metadata{};
    metadata.flags = 4;
    const auto result =
        encode_v1(out.data(), 32, *m.prepared(), metadata, strict, FormatHashDispatch::automatic());
    ASSERT_FALSE(result.succeeded());
    EXPECT_EQ(*result.failure(), EncodeError::unknown_flags);
    EXPECT_EQ(result.bytes_written(), 0U);
    EXPECT_EQ(out, before);
}
TEST(EncodeErrors, reserved_timestamp_status) {
    const auto m = measure_record({}, 4096U);
    ASSERT_TRUE(m.succeeded());
    std::array<std::byte, 32> out;
    out.fill(std::byte{0xA5});
    const auto before = out;
    RecordMetadata metadata{};
    metadata.flags = 3;
    const auto result =
        encode_v1(out.data(), 32, *m.prepared(), metadata, strict, FormatHashDispatch::automatic());
    ASSERT_FALSE(result.succeeded());
    EXPECT_EQ(*result.failure(), EncodeError::reserved_timestamp_status);
    EXPECT_EQ(result.bytes_written(), 0U);
    EXPECT_EQ(out, before);
}
TEST(EncodeErrors, invalid_time_value) {
    const auto m = measure_record({}, 4096U);
    ASSERT_TRUE(m.succeeded());
    std::array<std::byte, 32> out;
    out.fill(std::byte{0xA5});
    const auto before = out;
    RecordMetadata metadata{};
    metadata.flags = 2;
    metadata.time_value = 1;
    const auto result =
        encode_v1(out.data(), 32, *m.prepared(), metadata, strict, FormatHashDispatch::automatic());
    ASSERT_FALSE(result.succeeded());
    EXPECT_EQ(*result.failure(), EncodeError::invalid_time_value);
    EXPECT_EQ(result.bytes_written(), 0U);
    EXPECT_EQ(out, before);
}
TEST(EncodeErrors, fallback_timestamp_not_configured) {
    const auto m = measure_record({}, 4096U);
    ASSERT_TRUE(m.succeeded());
    std::array<std::byte, 32> out;
    out.fill(std::byte{0xA5});
    const auto before = out;
    RecordMetadata metadata{};
    metadata.flags = 1;
    const auto result =
        encode_v1(out.data(), 32, *m.prepared(), metadata, strict, FormatHashDispatch::automatic());
    ASSERT_FALSE(result.succeeded());
    EXPECT_EQ(*result.failure(), EncodeError::fallback_timestamp_not_configured);
    EXPECT_EQ(result.bytes_written(), 0U);
    EXPECT_EQ(out, before);
}
}  // namespace
