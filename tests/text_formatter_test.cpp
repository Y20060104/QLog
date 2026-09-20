#include "qlog/detail/text_formatter.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "qlog/detail/record_decoder.hpp"
#include "qlog/detail/record_encoder.hpp"

namespace {
using namespace qlog::detail;
const std::byte* bytes(std::string_view text) {
    return reinterpret_cast<const std::byte*>(text.data());
}
DecodedArg scalar(ArgumentTag tag, std::uint64_t bits) {
    DecodedArg arg{};
    arg.tag = tag;
    arg.value.bits = bits;
    return arg;
}
DecodedArg integer(std::int64_t value) {
    return scalar(ArgumentTag::Int64, std::bit_cast<std::uint64_t>(value));
}
DecodedArg real(double value) {
    return scalar(ArgumentTag::F64, std::bit_cast<std::uint64_t>(value));
}
DecodedArg real(float value) {
    return scalar(ArgumentTag::F32, std::bit_cast<std::uint32_t>(value));
}
DecodedArg string(std::string_view value) {
    DecodedArg arg{};
    arg.tag = ArgumentTag::Utf8String;
    arg.value.bytes = bytes(value);
    arg.byte_count = static_cast<std::uint32_t>(value.size());
    return arg;
}

void expect_text(std::string_view format, const std::vector<DecodedArg>& args,
                 std::string_view expected) {
    // Test both generous capacity and exact final size, with surrounding canaries.
    for (const auto capacity : {expected.size(), expected.size() + 128U}) {
        std::vector<std::byte> output(capacity + 2U, std::byte{0xA5});
        const auto result = render_message_utf8(bytes(format), format.size(), args.data(),
                                                args.size(), output.data() + 1U, capacity);
        ASSERT_FALSE(result.failure.has_value()) << format;
        EXPECT_EQ(result.size, expected.size()) << format;
        EXPECT_EQ(std::string(reinterpret_cast<const char*>(output.data() + 1U), result.size),
                  expected)
            << format;
        EXPECT_EQ(output.front(), std::byte{0xA5});
        EXPECT_TRUE(std::all_of(output.begin() + static_cast<std::ptrdiff_t>(1U + result.size),
                                output.end(), [](auto ch) { return ch == std::byte{0xA5}; }));
    }
}

TEST(TextFormatter, ZeroArgumentsPreserveRawBytes) {
    expect_text("{{x}}{}", {}, "{{x}}{}");
    expect_text(std::string_view("a\0{}", 4), {}, std::string_view("a\0{}", 4));
    const auto result = render_message_utf8(nullptr, 0U, nullptr, 0U, nullptr, 0U);
    EXPECT_FALSE(result.failure);
    EXPECT_EQ(result.size, 0U);
}

TEST(TextFormatter, SequentialArgumentsAndBraceScanner) {
    expect_text("{name}/{0}/{}", {integer(1), integer(2)}, "1/2/{}");
    expect_text("{} }} {{}} tail", {integer(7)}, "7 } {{} tail");
    expect_text("{{}}", {integer(7)}, "{7}");
    expect_text("a{bad{}}z", {integer(7)}, "a{bad7}z");
    expect_text("no placeholders}}", {integer(7)}, "no placeholders}");
    expect_text("{}", {integer(7), integer(8)}, "7");
    expect_text("{" + std::string(19, 'a') + "}", {integer(7)}, "7");
    const auto too_long = "{" + std::string(20, 'a') + "}";
    expect_text(too_long, {integer(7)}, too_long);
}

TEST(TextFormatter, ParserRetainsNonstandardFillAndWindowRules) {
    expect_text("{:O5}", {integer(7)}, "OOOO7");
    expect_text("{:D5}", {integer(7)}, "DDDD7");
    expect_text("{:>>5}", {integer(7)}, ">>>>7");
    expect_text("{:<>5}", {integer(7)}, "7    ");
    expect_text("{:123}", {integer(7)}, std::string(11, ' ') + "7");
    // Index 11 exits the internal parser; numeric finalization still restores 12.
    expect_text("{:12dddddddddddd}", {integer(7)}, std::string(11, ' ') + "7");
    expect_text("{:03}", {integer(7)}, "007");
    expect_text("{:.2}", {string("abcdef")}, "abcdef");
}

TEST(TextFormatter, AllIntegerWidthsSignsAndExtrema) {
    const std::vector<DecodedArg> args{scalar(ArgumentTag::Int8, 0x80U),
                                       scalar(ArgumentTag::UInt8, 0xFFU),
                                       scalar(ArgumentTag::Int16, 0x8000U),
                                       scalar(ArgumentTag::UInt16, 0xFFFFU),
                                       scalar(ArgumentTag::Int32, 0x80000000U),
                                       scalar(ArgumentTag::UInt32, 0xFFFFFFFFU),
                                       integer(INT64_MIN),
                                       scalar(ArgumentTag::UInt64, UINT64_MAX)};
    expect_text(
        "{} {} {} {} {} {} {} {}", args,
        "-128 255 -32768 65535 -2147483648 4294967295 -9223372036854775808 18446744073709551615");
    expect_text("{:+}/{:+}/{:+}",
                {integer(0), scalar(ArgumentTag::UInt64, 0), scalar(ArgumentTag::UInt64, 1)},
                "+0/0/+1");
    expect_text("{:x}", {integer(INT64_MIN)}, "-8000000000000000");
    expect_text("{:b}", {scalar(ArgumentTag::UInt64, UINT64_MAX)}, std::string(64, '1'));
    expect_text("{:b}", {integer(INT64_MIN)}, "-1" + std::string(63, '0'));
}

TEST(TextFormatter, RandomIntegerDigitsMatchIndependentToChars) {
    std::mt19937_64 random(170919U);
    for (unsigned iteration = 0; iteration < 2000U; ++iteration) {
        const auto value = random();
        for (const auto base : {2, 8, 10, 16}) {
            char buffer[65];
            auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value, base);
            ASSERT_EQ(converted.ec, std::errc{});
            const std::string_view expected(buffer,
                                            static_cast<std::size_t>(converted.ptr - buffer));
            const auto format = base == 2    ? "{:b}"
                                : base == 8  ? "{:o}"
                                : base == 16 ? "{:x}"
                                             : "{}";
            expect_text(format, {scalar(ArgumentTag::UInt64, value)}, expected);
        }
        char buffer[32];
        const auto signed_value = std::bit_cast<std::int64_t>(value);
        const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), signed_value);
        expect_text("{}", {integer(signed_value)},
                    std::string_view(buffer, static_cast<std::size_t>(converted.ptr - buffer)));
    }
}

TEST(TextFormatter, PrefixPointerAndPaddingCompatibility) {
    expect_text("{:#x}/{:#X}/{:#b}/{:#B}/{:#o}",
                {integer(42), integer(42), integer(5), integer(5), integer(8)},
                "0x2a/0X2A/0b101/0B101/10");
    expect_text("{} / {} / {:d} / {:#x}",
                {scalar(ArgumentTag::Pointer64, 0), scalar(ArgumentTag::Pointer64, 0xAB),
                 scalar(ArgumentTag::Pointer64, 0xAB), scalar(ArgumentTag::Pointer64, 0xAB)},
                "null / 0xAB / 0xab / 0x0xab");
    expect_text("{:05}", {integer(-42)}, "-0042");
    expect_text("{:#06x}", {integer(42)}, "0x002a");
    expect_text("{:^6}", {string("abc")}, "  abc ");
    expect_text("{:<6}", {string("abc")}, "abc   ");
    expect_text("{:*>6}", {string("abc")}, "***abc");
    expect_text("{:05}", {string("a-b")}, "a0a0b");
    expect_text("{:5}", {string("")}, "     ");
}

TEST(TextFormatter, BooleansCharactersNullAndEmbeddedBytes) {
    expect_text("{:x}/{:d}/{}",
                {scalar(ArgumentTag::Bool, 1), scalar(ArgumentTag::Bool, 0),
                 scalar(ArgumentTag::NullUtf8, 0)},
                "TRUE/FALSE/null");
    const std::string raw("a\0\xff", 3);
    expect_text("{}{}", {scalar(ArgumentTag::Char, 0xFF), string(raw)},
                std::string("\xff", 1) + raw);
    expect_text(std::string_view("a\0{}", 4), {integer(7)},
                std::string_view("a\0"
                                 "7",
                                 3));
    expect_text("{}", {string(std::string_view{})}, "");
}

TEST(TextFormatter, FloatingCompatibilityTruncatesAndRetainsHistoricalSigns) {
    expect_text("{}|{}", {real(1.5F), real(1.5)}, "1.5000000|1.500000000000000");
    expect_text("{:.2f}|{:.2F}", {real(1.875), real(1.875F)}, "1.87|1.87");
    expect_text("{}|{}|{:+.1}", {real(-0.5), real(-0.0), real(-0.5)},
                "0.500000000000000|0.000000000000000|+0.5");
    expect_text("{:3}|{:2}", {real(12.5), real(123.5)}, " 12|123");
    expect_text("{:.0}", {real(12.5)}, "12");
    expect_text("{:.99}", {real(0.0)}, "0." + std::string(99, '0'));
    expect_text("{}", {real(std::numeric_limits<double>::denorm_min())}, "0.000000000000000");
}

TEST(TextFormatter, ScientificSafeDomainAndExactFinalCapacity) {
    expect_text("{:8e}", {integer(12345)}, "1.234e+03");
    expect_text("{:8E}", {integer(-12345)}, "-1.234E+00");
    expect_text("{:3e}", {integer(12345)}, "1e+03");
    expect_text("{:8.2e}", {real(123.5)}, "1.23e+02");
    expect_text("{:8.2E}", {real(123.5F)}, "1.23E+02");
    // BQLog counts the sign in int_width, so even the dot placement is unusual.
    expect_text("{:8.2e}", {real(-12.5)}, "-.12e+02");
}

TEST(TextFormatter, FloatingSafetyExtensions) {
    expect_text("{}|{}|{}", {real(std::nan("")), real(INFINITY), real(-INFINITY)}, "nan|inf|-inf");
    expect_text("{:+08E}", {real(-INFINITY)}, "0000-inf");
    for (const auto value : {std::numeric_limits<double>::max(),
                             -std::numeric_limits<double>::max(), 0x1p64, -0x1p64}) {
        char expected[512];
        const auto converted = std::to_chars(expected, expected + sizeof(expected), value,
                                             std::chars_format::fixed, 99);
        ASSERT_EQ(converted.ec, std::errc{});
        expect_text("{:+#.99E}", {real(value)},
                    std::string_view(expected, static_cast<std::size_t>(converted.ptr - expected)));
    }
    expect_text("{:.0}", {real(-0x1p63)}, "-9223372036854775808");
    expect_text("{:.0}", {real(std::nextafter(0x1p64, 0.0))}, "18446744073709549568");
}

TEST(TextFormatter, UnsafeNumericStatesReportFailureAndArgumentPosition) {
    const std::array<std::pair<std::string_view, DecodedArg>, 3> cases{
        {{"x{:e}", integer(1)}, {"x{:1}", real(123.0F)}, {"x{:#05x}", string("x")}}};
    for (const auto& [format, arg] : cases) {
        std::array<std::byte, 128> output{};
        const auto result = render_message_utf8(bytes(format), format.size(), &arg, 1U,
                                                output.data(), output.size());
        ASSERT_TRUE(result.failure) << format;
        EXPECT_EQ(result.size, 0U);
        EXPECT_EQ(result.failure->error, FormatError::number_conversion_failed);
        EXPECT_EQ(result.failure->byte_offset, 1U);
        EXPECT_EQ(result.failure->argument_index, 0U);
    }
}

TEST(TextFormatter, InvalidMetadataPreflightDoesNotWrite) {
    auto good = integer(1);
    std::array<std::byte, 16> output;
    output.fill(std::byte{0xA5});
    auto check = [&](FormatResult result, FormatError error) {
        ASSERT_TRUE(result.failure);
        EXPECT_EQ(result.size, 0U);
        EXPECT_EQ(result.failure->error, error);
        EXPECT_TRUE(
            std::all_of(output.begin(), output.end(), [](auto c) { return c == std::byte{0xA5}; }));
    };
    check(render_message_utf8(nullptr, 1U, &good, 1U, output.data(), output.size()),
          FormatError::invalid_format_metadata);
    check(render_message_utf8(bytes(""), 8193U, &good, 1U, output.data(), output.size()),
          FormatError::format_too_large);
    check(render_message_utf8(bytes(""), 0U, nullptr, 1U, output.data(), output.size()),
          FormatError::invalid_arguments);
    check(render_message_utf8(bytes(""), 0U, &good, 33U, output.data(), output.size()),
          FormatError::invalid_arguments);
    check(render_message_utf8(bytes(""), 0U, &good, 1U, nullptr, 1U),
          FormatError::invalid_workspace);
    for (auto arg : {scalar(ArgumentTag::Invalid, 0), scalar(static_cast<ArgumentTag>(255), 0),
                     scalar(ArgumentTag::Bool, 2)}) {
        check(render_message_utf8(bytes("abc"), 3U, &arg, 1U, output.data(), output.size()),
              FormatError::invalid_arguments);
    }
    auto bad_string = string("");
    bad_string.value.bytes = nullptr;
    bad_string.byte_count = 1U;
    check(render_message_utf8(bytes(""), 0U, &bad_string, 1U, output.data(), output.size()),
          FormatError::invalid_arguments);
    std::array<DecodedArg, 2> extra{good, scalar(ArgumentTag::Bool, 2)};
    const auto extra_result = render_message_utf8(bytes("{}"), 2U, extra.data(), extra.size(),
                                                  output.data(), output.size());
    check(extra_result, FormatError::invalid_arguments);
    ASSERT_TRUE(extra_result.failure);
    EXPECT_EQ(extra_result.failure->argument_index, 1U);
}

TEST(TextFormatter, OutputLimitsAndGuardBytes) {
    const std::string text(65536U, 'a');
    auto arg = string(text);
    expect_text("{}", {arg}, text);
    std::vector<std::byte> output(65539U, std::byte{0xA5});
    for (const auto capacity : {0U, 1U, 65535U}) {
        const auto result =
            render_message_utf8(bytes("{}"), 2U, &arg, 1U, output.data() + 1U, capacity);
        ASSERT_TRUE(result.failure);
        EXPECT_EQ(result.failure->error, FormatError::text_output_limit_exceeded);
        EXPECT_EQ(result.size, 0U);
        EXPECT_EQ(output.front(), std::byte{0xA5});
        EXPECT_EQ(output[capacity + 1U], std::byte{0xA5});
    }
    const auto over = render_message_utf8(bytes("{}z"), 3U, &arg, 1U, output.data() + 1U, 65537U);
    ASSERT_TRUE(over.failure);
    EXPECT_EQ(over.failure->error, FormatError::text_output_limit_exceeded);
    EXPECT_EQ(output[65537U], std::byte{0xA5});
    const auto empty = render_message_utf8(bytes("a"), 1U, nullptr, 0U, nullptr, 0U);
    ASSERT_TRUE(empty.failure);
    EXPECT_EQ(empty.failure->error, FormatError::text_output_limit_exceeded);
    expect_text(std::string(8192U, 'z'), {}, std::string(8192U, 'z'));
}

TEST(TextFormatter, EncoderDecoderFormatterPreservesWireAndRawFormat) {
    constexpr std::string_view format = "{{{name}}} {:#x} {} {}";
    const auto measured = measure_record({bytes(format), format.size(), 0U}, 4096U, std::int8_t{-7},
                                         std::uint64_t{42}, "a\0b", true);
    ASSERT_TRUE(measured.succeeded());
    std::vector<std::byte> wire(measured.prepared()->payload_size());
    const RecordValidationPolicy policy{{~0ULL, ~0ULL, ~0ULL, ~0ULL}, true};
    const auto encoded = encode_v1(wire.data(), wire.size(), *measured.prepared(), RecordMetadata{},
                                   policy, FormatHashDispatch::automatic());
    ASSERT_TRUE(encoded.succeeded());
    std::array<DecodedArg, 32> workspace{};
    const auto decoded =
        decode_v1(wire.data(), wire.size(), workspace.data(), workspace.size(), policy);
    ASSERT_TRUE(decoded.succeeded());
    const auto& record = *decoded.record();
    EXPECT_EQ(std::memcmp(record.format_data(), bytes(format), format.size()), 0);
    expect_text(format,
                std::vector<DecodedArg>(record.arguments_data(),
                                        record.arguments_data() + record.argument_count()),
                std::string("{{-7} 0x2a a\0b TRUE", 19));
}

TEST(TextFormatter, RandomFormatsAreBoundedAndDeterministic) {
    std::mt19937_64 random(0xA01919U);
    constexpr std::string_view alphabet = "abc{}:<>^+-#.0123456789xXbBoOdDfFeE\0";
    const std::string raw("x-+B\0", 5);
    for (unsigned iteration = 0; iteration < 10000U; ++iteration) {
        std::string format;
        const auto length = random() % 100U;
        for (std::size_t index = 0; index < length; ++index)
            format.push_back(alphabet[random() % alphabet.size()]);
        std::array<DecodedArg, 4> args{
            integer(std::bit_cast<std::int64_t>(random())), real(std::bit_cast<double>(random())),
            string(raw), real(std::bit_cast<float>(static_cast<std::uint32_t>(random())))};
        const auto capacity = static_cast<std::size_t>(random() % 256U);
        std::array<std::byte, 258> first;
        std::array<std::byte, 258> second;
        first.fill(std::byte{0xA5});
        second.fill(std::byte{0x5A});
        const auto a = render_message_utf8(bytes(format), format.size(), args.data(), args.size(),
                                           first.data() + 1U, capacity);
        const auto b = render_message_utf8(bytes(format), format.size(), args.data(), args.size(),
                                           second.data() + 1U, capacity);
        ASSERT_EQ(a.failure.has_value(), b.failure.has_value());
        EXPECT_EQ(a.size, b.size);
        EXPECT_EQ(first.front(), std::byte{0xA5});
        EXPECT_EQ(second.front(), std::byte{0x5A});
        EXPECT_TRUE(std::all_of(first.begin() + static_cast<std::ptrdiff_t>(capacity + 1U),
                                first.end(), [](auto v) { return v == std::byte{0xA5}; }));
        EXPECT_TRUE(std::all_of(second.begin() + static_cast<std::ptrdiff_t>(capacity + 1U),
                                second.end(), [](auto v) { return v == std::byte{0x5A}; }));
        if (!a.failure) {
            EXPECT_LE(a.size, capacity);
            EXPECT_EQ(std::memcmp(first.data() + 1U, second.data() + 1U, a.size), 0) << format;
        } else
            EXPECT_EQ(a.size, 0U);
    }
}
}  // namespace
