#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <qlog/detail/record_decoder.hpp>
#include <qlog/detail/record_encoder.hpp>
#include <vector>

namespace {
using namespace qlog::detail;
using Bytes = std::vector<std::byte>;
const RecordValidationPolicy all{{~0ULL, ~0ULL, ~0ULL, ~0ULL}, true};
const RecordValidationPolicy primary{{1ULL, 0ULL, 0ULL, 0ULL}, false};
void put(Bytes& b, std::size_t offset, std::uint64_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i)
        b[offset + i] = static_cast<std::byte>((value >> (8U * i)) & 255U);
}
Bytes record(const Bytes& args = {}, std::uint16_t count = 0U, std::size_t format = 0U) {
    Bytes b(32U + format + args.size());
    put(b, 16U, format, 4U);
    put(b, 20U, args.size(), 4U);
    put(b, 28U, count, 2U);
    std::copy(args.begin(), args.end(), b.begin() + static_cast<std::ptrdiff_t>(32U + format));
    return b;
}
void failure(const Bytes& b, DecodeError error, std::size_t offset, std::uint8_t index = 0xFFU,
             const RecordValidationPolicy& policy = all) {
    std::array<DecodedArg, 32> workspace{};
    const auto r = decode_v1(b.data(), b.size(), workspace.data(), workspace.size(), policy);
    ASSERT_FALSE(r.succeeded());
    EXPECT_EQ(r.record(), nullptr);
    ASSERT_NE(r.failure(), nullptr);
    EXPECT_EQ(r.failure()->error, error);
    EXPECT_EQ(r.failure()->error_offset, offset);
    EXPECT_EQ(r.failure()->argument_index, index);
}

TEST(RecordDecoder, IndependentGoldenAllTagsAndUnalignedInputs) {
    const std::array<unsigned, 13> widths{1, 1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 8};
    Bytes args;
    std::array<std::uint64_t, 13> expected{};
    for (unsigned t = 1; t <= 13; ++t) {
        args.push_back(static_cast<std::byte>(t));
        for (unsigned j = 0; j < widths[t - 1]; ++j) {
            const unsigned v = t == 1 ? 1U : 0xF0U + j;
            args.push_back(static_cast<std::byte>(v));
            expected[t - 1] |= std::uint64_t{v} << (8U * j);
        }
    }
    const Bytes strings{std::byte{14}, std::byte{3}, std::byte{0},   std::byte{0},  std::byte{0},
                        std::byte{65}, std::byte{0}, std::byte{255}, std::byte{15}, std::byte{14},
                        std::byte{0},  std::byte{0}, std::byte{0},   std::byte{0}};
    args.insert(args.end(), strings.begin(), strings.end());
    auto b = record(args, 16, 3);
    put(b, 0, 0x0123456789ABCDEFULL, 8);
    put(b, 24, 0xFEDCBA98U, 4);
    put(b, 30, 200U, 1);
    b[32] = std::byte{65};
    b[33] = std::byte{0};
    b[34] = std::byte{255};
    for (std::size_t offset = 0; offset < 32; ++offset) {
        auto storage = std::make_unique<std::byte[]>(b.size() + offset);
        auto* p = storage.get() + offset;
        std::copy(b.begin(), b.end(), p);
        std::array<DecodedArg, 32> workspace{};
        const auto r = decode_v1(p, b.size(), workspace.data(), 32, all);
        ASSERT_TRUE(r.succeeded());
        EXPECT_EQ(r.failure(), nullptr);
        EXPECT_EQ(r.record()->header().time_value, 0x0123456789ABCDEFULL);
        EXPECT_EQ(r.record()->header().category_id, 0xFEDCBA98U);
        EXPECT_EQ(r.record()->header().level, 200U);
        EXPECT_EQ(r.record()->header().format_hash, 0U);
        EXPECT_EQ(r.record()->format_data(), p + 32);
        EXPECT_EQ(r.record()->format_size(), 3U);
        EXPECT_EQ(r.record()->arguments_data(), workspace.data());
        EXPECT_EQ(r.record()->argument_count(), 16U);
        for (unsigned t = 1; t <= 13; ++t) {
            EXPECT_EQ(workspace[t - 1].tag, static_cast<ArgumentTag>(t));
            EXPECT_EQ(workspace[t - 1].value.bits, expected[t - 1]);
            EXPECT_EQ(workspace[t - 1].byte_count, 0U);
        }
        EXPECT_EQ(workspace[13].byte_count, 3U);
        EXPECT_EQ(workspace[13].value.bytes, p + b.size() - 9);
        EXPECT_EQ(std::memcmp(workspace[13].value.bytes, strings.data() + 5, 3), 0);
        EXPECT_EQ(workspace[14].tag, ArgumentTag::NullUtf8);
        EXPECT_EQ(workspace[14].value.bits, 0U);
        EXPECT_EQ(workspace[15].tag, ArgumentTag::Utf8String);
        EXPECT_EQ(workspace[15].value.bytes, p + b.size());
        EXPECT_EQ(workspace[15].byte_count, 0U);
        p[0] = std::byte{0};
        EXPECT_EQ(r.record()->header().time_value, 0x0123456789ABCDEFULL);
    }
}

TEST(RecordDecoder, EntryValidationAndAllHeaderTruncations) {
    std::array<DecodedArg, 32> w{};
    auto r = decode_v1(nullptr, 1, nullptr, 0, all);
    EXPECT_EQ(r.failure()->error, DecodeError::invalid_payload_metadata);
    r = decode_v1(nullptr, 0, nullptr, 32, all);
    EXPECT_EQ(r.failure()->error, DecodeError::invalid_workspace);
    auto b = record();
    for (const auto n : {0U, 31U, 33U}) {
        r = decode_v1(b.data(), b.size(), w.data(), n, all);
        EXPECT_EQ(r.failure()->error, DecodeError::invalid_workspace);
    }
    for (std::size_t n = 0; n < 32; ++n) failure(Bytes(n), DecodeError::record_too_small, n);
}

TEST(RecordDecoder, MetadataMappingAndPriority) {
    auto b = record();
    b[30] = std::byte{1};
    b[31] = std::byte{255};
    failure(b, DecodeError::invalid_level, 30, 255, primary);
    b[30] = std::byte{0};
    failure(b, DecodeError::unknown_flags, 31, 255, primary);
    b[31] = std::byte{3};
    failure(b, DecodeError::reserved_timestamp_status, 31);
    b[31] = std::byte{2};
    b[0] = std::byte{1};
    failure(b, DecodeError::invalid_time_value, 0);
    b[31] = std::byte{1};
    failure(b, DecodeError::fallback_timestamp_not_configured, 31, 255, primary);
}

TEST(RecordDecoder, RegionsAndCountLimits) {
    auto b = record();
    put(b, 16, 8193, 4);
    failure(b, DecodeError::format_too_large, 16);
    put(b, 16, 1, 4);
    failure(b, DecodeError::invalid_format_length, 16);
    put(b, 16, 0, 4);
    put(b, 20, 1, 4);
    put(b, 28, 33, 2);
    failure(b, DecodeError::invalid_args_length, 20);
    put(b, 20, 0, 4);
    failure(b, DecodeError::arg_count_exceeded, 28);
    failure(record({}, 1), DecodeError::decoded_count_mismatch, 32, 0);
    failure(record({std::byte{15}}, 2), DecodeError::decoded_count_mismatch, 33, 1);
    failure(record({std::byte{15}}, 0), DecodeError::trailing_args_bytes, 32, 0);
    failure(record(Bytes(33, std::byte{15}), 32), DecodeError::trailing_args_bytes, 64, 32);
    for (const auto n : {0U, 32U}) {
        auto valid = record(Bytes(n, std::byte{15}), static_cast<std::uint16_t>(n), 8192);
        std::array<DecodedArg, 33> w{};
        w[32].value.bits = 0xABCDEFU;
        const auto r = decode_v1(valid.data(), valid.size(), w.data(), 32, all);
        ASSERT_TRUE(r.succeeded());
        EXPECT_EQ(r.record()->argument_count(), n);
        EXPECT_EQ(w[32].value.bits, 0xABCDEFU);
    }
}

TEST(RecordDecoder, RejectsEveryUnknownTagAndInvalidBool) {
    for (unsigned t = 0; t <= 255; ++t) {
        if (t >= 1 && t <= 15) continue;
        failure(record({std::byte{15}, static_cast<std::byte>(t)}, 2),
                DecodeError::invalid_or_unknown_tag, 33, 1);
    }
    for (unsigned v = 2; v <= 255; ++v)
        failure(record({std::byte{1}, static_cast<std::byte>(v)}, 1), DecodeError::invalid_bool, 33,
                0);
}

TEST(RecordDecoder, EveryScalarAndStringTruncationWithConsistentOuterLength) {
    const std::array<unsigned, 13> widths{1, 1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 8};
    for (unsigned t = 1; t <= 13; ++t) {
        for (unsigned n = 0; n < widths[t - 1]; ++n) {
            Bytes a{std::byte{15}, static_cast<std::byte>(t)};
            a.resize(2U + n);
            failure(record(a, 2), DecodeError::truncated_value, 34U + n, 1);
        }
    }
    for (unsigned n = 0; n < 4; ++n) {
        Bytes a(1U + n);
        a[0] = std::byte{14};
        failure(record(a, 1), DecodeError::truncated_string_length, 33U + n, 0);
    }
    for (unsigned n = 0; n < 3; ++n) {
        Bytes a(5U + n);
        a[0] = std::byte{14};
        a[1] = std::byte{3};
        failure(record(a, 1), DecodeError::truncated_string, 37U + n, 0);
    }
    failure(
        record({std::byte{14}, std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}}, 1),
        DecodeError::truncated_string, 37, 0);
}

TEST(RecordDecoder, FailureDoesNotCommitIncompleteArgumentOrExposeView) {
    auto b = record({std::byte{15}, std::byte{9}, std::byte{0}}, 2);
    std::array<DecodedArg, 32> w{};
    for (auto& a : w) a.value.bits = 0xDEADBEEFU;
    const auto r = decode_v1(b.data(), b.size(), w.data(), 32, all);
    ASSERT_FALSE(r.succeeded());
    EXPECT_EQ(r.record(), nullptr);
    EXPECT_EQ(w[0].tag, ArgumentTag::NullUtf8);
    for (std::size_t i = 1; i < w.size(); ++i) EXPECT_EQ(w[i].value.bits, 0xDEADBEEFU);
}

TEST(RecordDecoder, EncoderRoundTripPreservesBitsAndStrings) {
    const auto m = measure_record({nullptr, 0, 0}, 4096U, std::int16_t{-2}, "a\0b", -0.0);
    ASSERT_TRUE(m.succeeded());
    Bytes b(m.prepared()->payload_size());
    const auto e = encode_v1(b.data(), b.size(), *m.prepared(), RecordMetadata{}, all,
                             FormatHashDispatch::automatic());
    ASSERT_TRUE(e.succeeded());
    std::array<DecodedArg, 32> w{};
    const auto d = decode_v1(b.data(), b.size(), w.data(), 32, all);
    ASSERT_TRUE(d.succeeded());
    EXPECT_EQ(w[0].value.bits, 65534U);
    EXPECT_EQ(w[1].byte_count, 3U);
    EXPECT_EQ(std::memcmp(w[1].value.bytes, "a\0b", 3), 0);
    EXPECT_EQ(w[2].value.bits, 0x8000000000000000ULL);
}
}  // namespace
