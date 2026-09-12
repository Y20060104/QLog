#include <gtest/gtest.h>

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <qlog/arguments.hpp>
#include <qlog/detail/argument_traits.hpp>
#include <qlog/detail/record_measure.hpp>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace {

using qlog::detail::ArgumentTag;
using qlog::detail::FormatInput;
using qlog::detail::measure_record;
using qlog::detail::MeasureError;
using qlog::detail::NormalizedArgument;

constexpr std::uint8_t kNoArgument = std::numeric_limits<std::uint8_t>::max();

enum class SignedEnum : std::int16_t { negative_two = -2 };

template <std::size_t... Index>
[[nodiscard]] auto measure_bools(std::index_sequence<Index...>, std::size_t max_payload_size) {
    return measure_record(FormatInput{}, max_payload_size, (static_cast<void>(Index), false)...);
}

template <std::size_t... Index>
[[nodiscard]] consteval bool can_measure_bools(std::index_sequence<Index...>) {
    return requires { measure_record(FormatInput{}, 4096U, (static_cast<void>(Index), false)...); };
}

static_assert(qlog::detail::SupportedArgument<const char (&)[4]>);
static_assert(qlog::detail::SupportedArgument<const char8_t (&)[4]>);
static_assert(!qlog::detail::SupportedArgument<const char (&)[]>);
static_assert(!qlog::detail::SupportedArgument<const char*>);
static_assert(!qlog::detail::SupportedArgument<std::byte>);
static_assert(can_measure_bools(std::make_index_sequence<32>{}));
static_assert(!can_measure_bools(std::make_index_sequence<33>{}));

void expect_failure(const auto& result, MeasureError error, std::uint8_t argument_index,
                    std::size_t byte_count) {
    ASSERT_FALSE(result.succeeded());
    EXPECT_EQ(result.prepared(), nullptr);
    ASSERT_NE(result.failure(), nullptr);
    EXPECT_EQ(result.failure()->error, error);
    EXPECT_EQ(result.failure()->argument_index, argument_index);
    EXPECT_EQ(result.failure()->byte_count, byte_count);
}

TEST(ArgumentModel, EncodedSizesAreExactAndFailuresPreserveOutput) {
    constexpr std::array cases{
        std::pair{ArgumentTag::Bool, 2U},      std::pair{ArgumentTag::Char, 2U},
        std::pair{ArgumentTag::Int8, 2U},      std::pair{ArgumentTag::UInt8, 2U},
        std::pair{ArgumentTag::Int16, 3U},     std::pair{ArgumentTag::UInt16, 3U},
        std::pair{ArgumentTag::Int32, 5U},     std::pair{ArgumentTag::UInt32, 5U},
        std::pair{ArgumentTag::F32, 5U},       std::pair{ArgumentTag::Int64, 9U},
        std::pair{ArgumentTag::UInt64, 9U},    std::pair{ArgumentTag::F64, 9U},
        std::pair{ArgumentTag::Pointer64, 9U}, std::pair{ArgumentTag::NullUtf8, 1U},
    };

    for (const auto& [tag, expected] : cases) {
        std::size_t output = 999U;
        EXPECT_TRUE(qlog::detail::encoded_size(NormalizedArgument{.tag = tag}, output));
        EXPECT_EQ(output, expected);
    }

    std::size_t output = 999U;
    EXPECT_TRUE(qlog::detail::encoded_size(
        NormalizedArgument{.byte_count = 3U, .tag = ArgumentTag::Utf8String}, output));
    EXPECT_EQ(output, 8U);

    output = 999U;
    EXPECT_FALSE(
        qlog::detail::encoded_size(NormalizedArgument{.tag = ArgumentTag::Invalid}, output));
    EXPECT_EQ(output, 999U);

    output = 999U;
    EXPECT_FALSE(qlog::detail::encoded_size(
        NormalizedArgument{.tag = static_cast<ArgumentTag>(0xFFU)}, output));
    EXPECT_EQ(output, 999U);
}

TEST(MeasureRecord, EmptyRecordUsesOnlyTheHeader) {
    const auto result = measure_record(FormatInput{}, 32U);

    ASSERT_TRUE(result.succeeded());
    EXPECT_EQ(result.failure(), nullptr);
    ASSERT_NE(result.prepared(), nullptr);
    EXPECT_EQ(result.prepared()->format_data(), nullptr);
    EXPECT_EQ(result.prepared()->format_size(), 0U);
    EXPECT_EQ(result.prepared()->argument_count(), 0U);
    EXPECT_EQ(result.prepared()->args_size(), 0U);
    EXPECT_EQ(result.prepared()->payload_size(), 32U);
    EXPECT_EQ(result.prepared()->max_payload_size(), 32U);
}

TEST(MeasureRecord, GlobalValidationHasDeterministicPriority) {
    const auto invalid_limits = measure_record(FormatInput{nullptr, 9000U, 0U}, 31U);
    expect_failure(invalid_limits, MeasureError::invalid_limits, kNoArgument, 31U);

    const auto invalid_format = measure_record(FormatInput{nullptr, 1U, 0U}, 64U);
    expect_failure(invalid_format, MeasureError::invalid_format_metadata, kNoArgument, 1U);

    std::array<std::byte, 8193U> format{};
    const auto format_too_large =
        measure_record(FormatInput{format.data(), format.size(), 0U}, 9000U);
    expect_failure(format_too_large, MeasureError::format_too_large, kNoArgument, format.size());

    if constexpr (std::numeric_limits<std::size_t>::max() >
                  std::numeric_limits<std::uint32_t>::max()) {
        constexpr std::size_t too_large =
            static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max()) + 1U;
        const auto limit_too_large = measure_record(FormatInput{}, too_large);
        expect_failure(limit_too_large, MeasureError::invalid_limits, kNoArgument, too_large);
    }
}

TEST(MeasureRecord, NormalizesScalarBitPatternsAndTags) {
    int object = 0;
    constexpr float negative_zero = -0.0F;
    constexpr double infinity = std::numeric_limits<double>::infinity();

    const auto result = measure_record(FormatInput{}, 256U, true, static_cast<char>(0xFF),
                                       std::int32_t{-1}, SignedEnum::negative_two, negative_zero,
                                       infinity, qlog::ptr(&object), nullptr);

    ASSERT_TRUE(result.succeeded());
    const auto* arguments = result.prepared()->arguments_data();
    EXPECT_EQ(arguments[0].tag, ArgumentTag::Bool);
    EXPECT_EQ(arguments[0].bits, 1U);
    EXPECT_EQ(arguments[1].tag, ArgumentTag::Char);
    EXPECT_EQ(arguments[1].bits, 0xFFU);
    EXPECT_EQ(arguments[2].tag, ArgumentTag::Int32);
    EXPECT_EQ(arguments[2].bits, std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(arguments[3].tag, ArgumentTag::Int16);
    EXPECT_EQ(arguments[3].bits, 0xFFFEU);
    EXPECT_EQ(arguments[4].tag, ArgumentTag::F32);
    EXPECT_EQ(arguments[4].bits, std::bit_cast<std::uint32_t>(negative_zero));
    EXPECT_EQ(arguments[5].tag, ArgumentTag::F64);
    EXPECT_EQ(arguments[5].bits, std::bit_cast<std::uint64_t>(infinity));
    EXPECT_EQ(arguments[6].tag, ArgumentTag::Pointer64);
    EXPECT_EQ(arguments[6].bits,
              static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&object)));
    EXPECT_EQ(arguments[7].tag, ArgumentTag::Pointer64);
    EXPECT_EQ(arguments[7].bits, 0U);
    EXPECT_EQ(result.prepared()->args_size(), 44U);
    EXPECT_EQ(result.prepared()->payload_size(), 76U);
}

TEST(MeasureRecord, UsesKnownStringLengthsWithoutScanningForEmbeddedNull) {
    const std::string embedded{"a\0b", 3U};
    const std::u8string_view utf8{u8"xy"};
    const std::string_view empty{};

    const auto result = measure_record(FormatInput{}, 128U, "abc", embedded, utf8, empty);

    ASSERT_TRUE(result.succeeded());
    const auto* arguments = result.prepared()->arguments_data();
    EXPECT_EQ(arguments[0].byte_count, 3U);
    EXPECT_EQ(arguments[1].byte_count, 3U);
    EXPECT_EQ(arguments[2].byte_count, 2U);
    EXPECT_EQ(arguments[3].byte_count, 0U);
    EXPECT_EQ(arguments[3].tag, ArgumentTag::Utf8String);
    EXPECT_EQ(result.prepared()->args_size(), 28U);
}

TEST(MeasureRecord, RejectsCharacterArrayWithoutTrailingNull) {
    const char invalid[3] = {'a', 'b', 'c'};
    const auto result = measure_record(FormatInput{}, 64U, invalid);
    expect_failure(result, MeasureError::invalid_string_metadata, 0U, 2U);
}

TEST(MeasureRecord, DistinguishesNullEmptyAndPointerNull) {
    const auto result =
        measure_record(FormatInput{}, 64U, qlog::cstr(nullptr), qlog::cstr(""), nullptr);

    ASSERT_TRUE(result.succeeded());
    const auto* arguments = result.prepared()->arguments_data();
    EXPECT_EQ(arguments[0].tag, ArgumentTag::NullUtf8);
    EXPECT_EQ(arguments[1].tag, ArgumentTag::Utf8String);
    EXPECT_EQ(arguments[1].byte_count, 0U);
    EXPECT_EQ(arguments[2].tag, ArgumentTag::Pointer64);
    EXPECT_EQ(result.prepared()->args_size(), 15U);
    EXPECT_EQ(result.prepared()->payload_size(), 47U);
}

TEST(MeasureRecord, UsesNulTerminatedCStrLengthAndCachesIt) {
    const char embedded_null[] = {'a', '\0', 'b', '\0'};
    const auto result = measure_record(FormatInput{}, 64U, qlog::cstr(embedded_null));

    ASSERT_TRUE(result.succeeded());
    const auto& normalized = result.prepared()->arguments_data()[0];
    EXPECT_EQ(normalized.tag, ArgumentTag::Utf8String);
    EXPECT_EQ(normalized.bytes, reinterpret_cast<const std::byte*>(embedded_null));
    EXPECT_EQ(normalized.byte_count, 1U);
    EXPECT_EQ(result.prepared()->payload_size(), 38U);
}

TEST(MeasureRecord, CStrLengthContributesToExactPayload) {
    const auto exact = measure_record(FormatInput{}, 40U, qlog::cstr("abc"));
    ASSERT_TRUE(exact.succeeded());
    EXPECT_EQ(exact.prepared()->arguments_data()[0].byte_count, 3U);
    EXPECT_EQ(exact.prepared()->payload_size(), 40U);

    const auto one_short = measure_record(FormatInput{}, 39U, qlog::cstr("abc"));
    expect_failure(one_short, MeasureError::payload_too_large, kNoArgument, 40U);
}

TEST(MeasureRecord, MultipleCStringsUseMeasuredLengths) {
    const auto exact = measure_record(FormatInput{}, 45U, qlog::cstr("ab"), qlog::cstr("c"));
    ASSERT_TRUE(exact.succeeded());
    EXPECT_EQ(exact.prepared()->payload_size(), 45U);

    const auto one_short = measure_record(FormatInput{}, 44U, qlog::cstr("ab"), qlog::cstr("c"));
    expect_failure(one_short, MeasureError::payload_too_large, kNoArgument, 45U);
}

TEST(MeasureRecord, ReportsParameterErrorsBeforeFinalQuota) {
    const char invalid_array[10] = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9'};

    const auto later_array = measure_record(FormatInput{}, 40U, qlog::cstr("abc"), invalid_array);
    expect_failure(later_array, MeasureError::invalid_string_metadata, 1U, 9U);

    const auto early_array =
        measure_record(FormatInput{}, 64U, invalid_array, qlog::cstr("not reached"));
    expect_failure(early_array, MeasureError::invalid_string_metadata, 0U, 9U);
}

TEST(MeasureRecord, ReportsFinalQuotaFailureAsNonParameterError) {
    const char format[] = "x";
    const FormatInput input{reinterpret_cast<const std::byte*>(format), 1U, 123U};

    const auto exact = measure_record(input, 40U, std::int32_t{7}, true);
    ASSERT_TRUE(exact.succeeded());
    EXPECT_EQ(exact.prepared()->args_size(), 7U);
    EXPECT_EQ(exact.prepared()->payload_size(), 40U);
    EXPECT_EQ(exact.prepared()->precomputed_stored_hash(), 123U);

    const auto one_short = measure_record(input, 39U, std::int32_t{7}, true);
    expect_failure(one_short, MeasureError::payload_too_large, kNoArgument, 40U);
}

TEST(MeasureRecord, AcceptsTheFrozenMaximumArgumentCount) {
    const auto result = measure_bools(std::make_index_sequence<32>{}, 96U);
    ASSERT_TRUE(result.succeeded());
    EXPECT_EQ(result.prepared()->argument_count(), 32U);
    EXPECT_EQ(result.prepared()->args_size(), 64U);
    EXPECT_EQ(result.prepared()->payload_size(), 96U);
}

}  // namespace
