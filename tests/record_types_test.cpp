#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <qlog/detail/record_types.hpp>
#include <type_traits>

namespace {
using namespace qlog::detail;
static_assert(!std::is_default_constructible_v<RecordValidationPolicy>);
static_assert(!std::is_default_constructible_v<EncodeResult>);
static_assert(!std::is_default_constructible_v<DecodeResult>);
static_assert(!std::is_default_constructible_v<DecodedRecordView>);
static_assert(!std::is_aggregate_v<EncodeResult>);
static_assert(!std::is_aggregate_v<DecodeResult>);
static_assert(!std::is_aggregate_v<DecodedRecordView>);

TEST(RecordTypes, EveryLevelBitIsIndependent) {
    for (unsigned allowed = 0; allowed < 256U; ++allowed) {
        std::array<std::uint64_t, 4> masks{};
        masks[allowed / 64U] = std::uint64_t{1} << (allowed % 64U);
        const RecordValidationPolicy policy{masks, false};
        for (unsigned level = 0; level < 256U; ++level) {
            EXPECT_EQ(policy.allows_level(static_cast<std::uint8_t>(level)), level == allowed);
            const auto error = record_metadata_impl::validate_record_metadata(
                {0U, 0U, static_cast<std::uint8_t>(level), 0U}, policy);
            EXPECT_EQ(error, level == allowed ? std::nullopt
                                              : std::optional{MetadataError::invalid_level});
        }
    }
}

TEST(RecordTypes, ExhaustiveFlagsTimestampAndFallbackContract) {
    for (bool fallback : {false, true}) {
        const RecordValidationPolicy policy{{1U, 0U, 0U, 0U}, fallback};
        for (unsigned flags = 0; flags < 256U; ++flags) {
            for (std::uint64_t time : {0ULL, 42ULL}) {
                std::optional<MetadataError> expected;
                if (flags >= 4U)
                    expected = MetadataError::unknown_flags;
                else if (flags == 3U)
                    expected = MetadataError::reserved_timestamp_status;
                else if (flags == 2U && time != 0U)
                    expected = MetadataError::invalid_time_value;
                else if (flags == 1U && !fallback)
                    expected = MetadataError::fallback_timestamp_not_configured;
                EXPECT_EQ(record_metadata_impl::validate_record_metadata(
                              {time, 0U, 0U, static_cast<std::uint8_t>(flags)}, policy),
                          expected);
                EXPECT_EQ(record_metadata_impl::validate_record_metadata(
                              {time, 0U, 1U, static_cast<std::uint8_t>(flags)}, policy),
                          MetadataError::invalid_level);
            }
        }
    }
}

TEST(RecordTypes, ResultsOwnHeaderAndBorrowViewsWithExclusiveSuccessOrFailure) {
    const auto success = RecordCodecAccess::encode_success(42U);
    EXPECT_TRUE(success.succeeded());
    EXPECT_EQ(success.failure(), nullptr);
    EXPECT_EQ(success.bytes_written(), 42U);
    const auto failure = RecordCodecAccess::encode_failure(EncodeError::invalid_level);
    EXPECT_FALSE(failure.succeeded());
    EXPECT_EQ(failure.bytes_written(), 0U);
    ASSERT_NE(failure.failure(), nullptr);
    EXPECT_EQ(*failure.failure(), EncodeError::invalid_level);
    RecordHeader header{};
    header.format_hash = 99U;
    header.format_bytes = 2U;
    header.arg_count = 1U;
    std::array<std::byte, 2> format{};
    std::array<DecodedArg, 32> workspace{};
    const auto decoded = RecordCodecAccess::decode_success(header, format.data(), workspace.data());
    header.format_hash = 0U;
    EXPECT_TRUE(decoded.succeeded());
    EXPECT_EQ(decoded.failure(), nullptr);
    ASSERT_NE(decoded.record(), nullptr);
    EXPECT_EQ(decoded.record()->header().format_hash, 99U);
    EXPECT_EQ(decoded.record()->format_data(), format.data());
    EXPECT_EQ(decoded.record()->format_size(), 2U);
    EXPECT_EQ(decoded.record()->arguments_data(), workspace.data());
    EXPECT_EQ(decoded.record()->argument_count(), 1U);
    const auto rejected =
        RecordCodecAccess::decode_failure({DecodeError::truncated_value, 37U, 2U});
    EXPECT_FALSE(rejected.succeeded());
    EXPECT_EQ(rejected.record(), nullptr);
    ASSERT_NE(rejected.failure(), nullptr);
    EXPECT_EQ(rejected.failure()->error, DecodeError::truncated_value);
    EXPECT_EQ(rejected.failure()->error_offset, 37U);
    EXPECT_EQ(rejected.failure()->argument_index, 2U);
}
}  // namespace
