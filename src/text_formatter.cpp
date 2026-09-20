/*
 * Format parsing and compatibility behavior adapted from BQLog layout.cpp.
 * Copyright (C) 2025 Tencent.
 * Licensed under the Apache License, Version 2.0.
 * https://www.apache.org/licenses/LICENSE-2.0
 * Distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND.
 * QLog changes: bounded byte writer, decoded argument ABI, safe numeric guards,
 * fixed stack conversion buffers and constant-base digit conversion.
 */
#include "qlog/detail/text_formatter.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

#include "qlog/detail/record_limits.hpp"

namespace qlog::detail {
namespace {
constexpr std::size_t kMaxMessageBytes = 65536U;
constexpr std::uint8_t kNoArgument = 0xFFU;

[[nodiscard]] FormatResult make_success(std::size_t size) noexcept {
    return FormatResult{size, std::nullopt};
}

[[nodiscard]] FormatResult make_failure(FormatError error, std::size_t byte_offset,
                                        std::uint8_t argument_index = kNoArgument) noexcept {
    return FormatResult{0U, FormatFailure{error, byte_offset, argument_index}};
}

[[nodiscard]] char read_char(const std::byte* data, std::size_t index) noexcept {
    return static_cast<char>(std::to_integer<unsigned char>(data[index]));
}

struct CheckedTextWriter {
    std::byte* data;
    std::size_t capacity;
    std::size_t used{0};

    [[nodiscard]] std::size_t remaining() const noexcept {
        return capacity - used;
    }

    [[nodiscard]] bool append_bytes(const void* source, std::size_t count) noexcept {
        if (count == 0U) {
            return true;
        }

        if (count > remaining()) {
            return false;
        }

        std::memcpy(data + used, source, count);
        used += count;
        return true;
    }

    [[nodiscard]] bool append_char(char value) noexcept {
        return append_bytes(&value, 1U);
    }

    [[nodiscard]] bool append_fill(char value, std::size_t count) noexcept {
        if (count == 0U) {
            return true;
        }

        if (count > remaining()) {
            return false;
        }

        std::memset(data + used, static_cast<unsigned char>(value), count);

        used += count;
        return true;
    }
};

bool copy_literal_run(const std::byte* format, std::size_t format_size, std::size_t& cursor,
                      CheckedTextWriter& writer) noexcept {
    const std::size_t begin = cursor;
    while (cursor < format_size) {
        const char ch = read_char(format, cursor);

        if (ch == '{' || ch == '}') {
            break;
        }

        ++cursor;
    }

    const std::size_t count = cursor - begin;
    if (count == 0U) {
        return true;
    }
    return writer.append_bytes(format + begin, count);
}
[[nodiscard]] FormatSpec parse_format_spec(const std::byte* style,
                                           std::size_t style_size) noexcept {
    FormatSpec spec{};
    spec.used = true;

    if (read_char(style, 0U) != ':') {
        return spec;
    }

    bool precision_mode = false;
    bool alignment_seen = false;
    bool digits_started = false;

    char first_digit = '0';
    char second_digit = '\0';

    const auto read_number = [&]() noexcept -> std::uint32_t {
        const auto first = static_cast<std::uint32_t>(first_digit - '0');

        if (second_digit == '\0') {
            return first;
        }

        return first * 10U + static_cast<std::uint32_t>(second_digit - '0');
    };

    for (std::size_t index = 1U;; ++index) {
        if (index >= style_size || index > 10U) {
            spec.offset = 0U;
            spec.width = 0U;
            break;
        }

        const char ch = read_char(style, index);

        if (ch == '{') {
            spec.offset = 0U;
            spec.width = 0U;
            break;
        }

        if (ch == '}') {
            spec.offset = static_cast<std::uint32_t>(index);
            break;
        }

        switch (ch) {
            case '#':
                spec.prefix = '#';
                break;

            case '<':
            case '>':
            case '^':
                if (!alignment_seen) {
                    spec.align = ch;
                    alignment_seen = true;
                } else {
                    spec.fill = ch;
                    if (spec.align == '<' || spec.align == '^') {
                        spec.fill = ' ';
                    }
                }
                break;

            case '+':
            case '-':
                spec.sign = ch;
                break;

            case 'b':
            case 'B':
            case 'e':
            case 'E':
            case 'f':
            case 'F':
            case 'x':
            case 'X':
            case 'o':
            case 'd':
                if (spec.type == ' ') {
                    spec.type = ch;
                    spec.upper = false;

                    if (ch == 'E') {
                        spec.type = 'e';
                        spec.upper = true;
                    } else if (ch == 'X') {
                        spec.type = 'x';
                        spec.upper = true;
                    } else if (ch == 'B') {
                        spec.type = 'b';
                        spec.upper = true;
                    }
                }
                if ((ch == 'x' || ch == 'X') && (spec.align != '>')) {
                    spec.fill = ' ';
                }
                break;
            case '.':
                precision_mode = true;
                spec.width = read_number();
                first_digit = '0';
                second_digit = '\0';
                break;

            default:
                if (!digits_started && (ch < '1' || ch > '9')) {
                    spec.fill = ch;

                    if (spec.align == '<' || spec.align == '^') {
                        spec.fill = ' ';
                    }
                } else if (ch >= '0' && ch <= '9') {
                    digits_started = true;

                    if (first_digit == '0') {
                        first_digit = ch;
                    } else if (second_digit == '\0') {
                        second_digit = ch;
                    }
                }

                break;
        }
    }

    if (precision_mode) {
        spec.precision = read_number();
    } else {
        spec.width = read_number();
    }

    if (spec.type == ' ') {
        spec.type = 'd';
    }
    return spec;
}
enum class PaddingMode : std::uint8_t { compatibility, plain };
using Error = std::optional<FormatError>;
constexpr auto kOutputFull = FormatError::text_output_limit_exceeded;
constexpr auto kBadNumber = FormatError::number_conversion_failed;

// Base is a compile-time constant: no runtime division in binary/octal/hex.
// Decimal division by ten is also visible to the optimizer as a constant.
template <unsigned Base>
char* write_digits(std::uint64_t value, char* end, bool upper) noexcept {
    const char* alphabet = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    do {
        *--end = alphabet[value % Base];
        value /= Base;
    } while (value != 0U);
    return end;
}

[[nodiscard]] Error finish_exponent(std::uint32_t exponent, std::size_t begin,
                                    const FormatSpec& spec, CheckedTextWriter& writer) noexcept {
    char digits[10];
    const char* first = write_digits<10>(exponent, digits + sizeof(digits), false);
    const auto count = static_cast<std::size_t>(digits + sizeof(digits) - first);
    // BQLog subtracts this in an unsigned cursor. Reject its unsafe domain.
    const auto mantissa_limit =
        static_cast<std::int64_t>(spec.width) - static_cast<std::int64_t>(count) - 2;
    if (mantissa_limit < 0) return kBadNumber;
    const auto target = begin + static_cast<std::size_t>(mantissa_limit);
    if (writer.used > target) {
        writer.used = target == begin ? begin + 1U : target;
    }
    if (!writer.append_char(spec.upper ? 'E' : 'e') || !writer.append_char('+') ||
        (count == 1U && !writer.append_char('0')) || !writer.append_bytes(first, count))
        return kOutputFull;
    return std::nullopt;
}

[[nodiscard]] Error render_magnitude(std::uint64_t value, unsigned default_base, bool negative,
                                     bool positive_sign, const FormatSpec& spec,
                                     CheckedTextWriter& writer) noexcept {
    if (negative || positive_sign) {
        if (!writer.append_char(negative ? '-' : '+')) return kOutputFull;
    }
    unsigned base = default_base;
    if (spec.type == 'b')
        base = 2;
    else if (spec.type == 'x')
        base = 16;
    else if (spec.type == 'o')
        base = 8;
    if (spec.prefix == '#' && (spec.type == 'b' || spec.type == 'x')) {
        if (!writer.append_char('0') ||
            !writer.append_char(spec.type == 'b' ? (spec.upper ? 'B' : 'b')
                                                 : (spec.upper ? 'X' : 'x')))
            return kOutputFull;
    }
    char digits[64];
    char* const end = digits + sizeof(digits);
    const char* first;
    switch (base) {
        case 2:
            first = write_digits<2>(value, end, spec.upper);
            break;
        case 8:
            first = write_digits<8>(value, end, spec.upper);
            break;
        case 16:
            first = write_digits<16>(value, end, spec.upper);
            break;
        default:
            first = write_digits<10>(value, end, spec.upper);
            break;
    }
    const auto count = static_cast<std::size_t>(end - first);
    if (spec.type != 'e') {
        return writer.append_bytes(first, count) ? Error{} : Error{kOutputFull};
    }
    // Preserve BQLog's quotient > base exponent test, including negative -> 0.
    std::uint32_t exponent = 0;
    if (!negative) {
        // Scientific integers use decimal, except the pointer's default base 16.
        if (base == 16U) {
            while ((value /= 16U) > 16U) ++exponent;
        } else {
            while ((value /= 10U) > 10U) ++exponent;
        }
    }
    const auto begin = writer.used;
    if (!writer.append_char(*first) || !writer.append_char('.') ||
        !writer.append_bytes(first + 1, count - 1U))
        return kOutputFull;
    return finish_exponent(exponent, begin, spec, writer);
}

[[nodiscard]] Error render_unsigned(std::uint64_t value, unsigned default_base,
                                    const FormatSpec& spec, CheckedTextWriter& writer) noexcept {
    return render_magnitude(value, default_base, false, value != 0U && spec.sign == '+', spec,
                            writer);
}

[[nodiscard]] Error render_signed(std::int64_t value, const FormatSpec& spec,
                                  CheckedTextWriter& writer) noexcept {
    const auto bits = static_cast<std::uint64_t>(value);
    const auto magnitude = value < 0 ? std::uint64_t{0} - bits : bits;
    return render_magnitude(magnitude, 10U, value < 0, value >= 0 && spec.sign == '+', spec,
                            writer);
}

template <typename Float>
[[nodiscard]] Error render_float(Float value, const FormatSpec& spec, CheckedTextWriter& writer,
                                 PaddingMode& mode) noexcept {
    constexpr int default_precision = sizeof(Float) == sizeof(float) ? 7 : 15;
    int precision =
        spec.precision == UINT32_MAX ? default_precision : static_cast<int>(spec.precision);
    const bool outside = value >= static_cast<Float>(0x1p64) || value < static_cast<Float>(-0x1p63);
    if (!std::isfinite(value) || outside) {
        mode = PaddingMode::plain;
        if (std::isnan(value)) return writer.append_bytes("nan", 3U) ? Error{} : Error{kOutputFull};
        if (std::isinf(value)) {
            const bool negative = std::signbit(value);
            return writer.append_bytes(negative ? "-inf" : "inf", negative ? 4U : 3U)
                       ? Error{}
                       : Error{kOutputFull};
        }
        char buffer[512];
        const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                          std::chars_format::fixed, precision);
        if (result.ec != std::errc{}) return kBadNumber;
        return writer.append_bytes(buffer, static_cast<std::size_t>(result.ptr - buffer))
                   ? Error{}
                   : Error{kOutputFull};
    }
    const auto begin = writer.used;
    const bool scientific = spec.type == 'e';
    auto integral_spec = spec;
    if (scientific) integral_spec.type = 'Z';
    if (value >= 0) {
        const auto integral = static_cast<std::uint64_t>(value);
        if (auto error = render_unsigned(integral, 10U, integral_spec, writer)) return error;
        value -= static_cast<Float>(integral);
    } else {
        const auto integral = static_cast<std::int64_t>(value);
        if (auto error = render_signed(integral, integral_spec, writer)) return error;
        value -= static_cast<Float>(integral);
    }
    const auto integral_width = writer.used - begin;
    if (spec.width > 0U && spec.width < static_cast<std::size_t>(precision) + 1U + integral_width) {
        precision = static_cast<int>(spec.width) - static_cast<int>(integral_width) - 1;
        if (precision < 0) {
            if constexpr (sizeof(Float) == sizeof(float))
                return kBadNumber;
            else
                precision = 0;
        }
    }
    value = std::fabs(value);
    std::optional<std::size_t> point;
    if (precision > 0 || (scientific && integral_width > 1U)) {
        point = writer.used;
        if (!writer.append_char('.')) return kOutputFull;
    }
    for (int index = 0; index < precision; ++index) {
        value *= static_cast<Float>(10);
        if (!(value >= 0 && value < 10)) return kBadNumber;
        const auto digit = static_cast<int>(value);
        value -= static_cast<Float>(digit);
        if (!writer.append_char(static_cast<char>('0' + digit))) return kOutputFull;
    }
    if (scientific) {
        const auto moves = integral_width - 1U;
        if (point) {
            if (*point - begin < moves) return kBadNumber;
            for (std::size_t index = 0; index < moves; ++index) {
                std::swap(writer.data[*point], writer.data[*point - 1U]);
                --*point;
            }
        }
        if (spec.precision > 0U && spec.precision < writer.used - begin) {
            if (!point) return kBadNumber;
            const auto target = *point + spec.precision + 1U;
            if (target > writer.used) return kBadNumber;
            writer.used = target;
        }
        return finish_exponent(static_cast<std::uint32_t>(moves), begin, spec, writer);
    }
    return std::nullopt;
}

[[nodiscard]] bool valid_argument(const DecodedArg& arg) noexcept {
    if (arg.tag < ArgumentTag::Bool || arg.tag > ArgumentTag::NullUtf8) return false;
    if (arg.tag == ArgumentTag::Utf8String)
        return arg.byte_count == 0U || arg.value.bytes != nullptr;
    return arg.tag != ArgumentTag::Bool || arg.value.bits <= 1U;
}

[[nodiscard]] Error render_argument(const DecodedArg& arg, const FormatSpec& spec,
                                    CheckedTextWriter& writer, PaddingMode& mode) noexcept {
    mode = PaddingMode::compatibility;
    switch (arg.tag) {
        case ArgumentTag::Bool:
            return writer.append_bytes(arg.value.bits ? "TRUE" : "FALSE", arg.value.bits ? 4U : 5U)
                       ? Error{}
                       : Error{kOutputFull};
        case ArgumentTag::Char: {
            const auto value = static_cast<unsigned char>(arg.value.bits);
            return writer.append_bytes(&value, 1U) ? Error{} : Error{kOutputFull};
        }
        case ArgumentTag::Utf8String:
            return writer.append_bytes(arg.value.bytes, arg.byte_count) ? Error{}
                                                                        : Error{kOutputFull};
        case ArgumentTag::NullUtf8:
            return writer.append_bytes("null", 4U) ? Error{} : Error{kOutputFull};
        default:
            break;
    }
    // Numeric intermediates may be longer than the final scientific field.
    // Keep them off the caller's output; 512 covers double + 99 fraction digits.
    std::byte buffer[512];
    CheckedTextWriter number{buffer, sizeof(buffer)};
    Error error;
    const auto bits = arg.value.bits;
    switch (arg.tag) {
        case ArgumentTag::Int8:
            error = render_signed(std::bit_cast<std::int8_t>(static_cast<std::uint8_t>(bits)), spec,
                                  number);
            break;
        case ArgumentTag::Int16:
            error = render_signed(std::bit_cast<std::int16_t>(static_cast<std::uint16_t>(bits)),
                                  spec, number);
            break;
        case ArgumentTag::Int32:
            error = render_signed(std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(bits)),
                                  spec, number);
            break;
        case ArgumentTag::Int64:
            error = render_signed(std::bit_cast<std::int64_t>(bits), spec, number);
            break;
        case ArgumentTag::UInt8:
            error = render_unsigned(static_cast<std::uint8_t>(bits), 10U, spec, number);
            break;
        case ArgumentTag::UInt16:
            error = render_unsigned(static_cast<std::uint16_t>(bits), 10U, spec, number);
            break;
        case ArgumentTag::UInt32:
            error = render_unsigned(static_cast<std::uint32_t>(bits), 10U, spec, number);
            break;
        case ArgumentTag::UInt64:
            error = render_unsigned(bits, 10U, spec, number);
            break;
        case ArgumentTag::F32:
            error = render_float(std::bit_cast<float>(static_cast<std::uint32_t>(bits)), spec,
                                 number, mode);
            break;
        case ArgumentTag::F64:
            error = render_float(std::bit_cast<double>(bits), spec, number, mode);
            break;
        case ArgumentTag::Pointer64:
            if (bits == 0U) {
                if (!number.append_bytes("null", 4U)) error = kOutputFull;
            } else {
                if (!number.append_bytes("0x", 2U)) return kOutputFull;
                error = render_unsigned(bits, 16U, spec, number);
            }
            break;
        default:
            return FormatError::invalid_arguments;
    }
    if (error) return error;
    return writer.append_bytes(buffer, number.used) ? Error{} : Error{kOutputFull};
}

[[nodiscard]] Error apply_padding(CheckedTextWriter& writer, std::size_t begin,
                                  const FormatSpec& spec, PaddingMode mode) noexcept {
    const auto length = writer.used - begin;
    if (length >= spec.width) return std::nullopt;
    const auto padding = spec.width - length;
    if (padding > writer.remaining()) return kOutputFull;
    const auto fill = static_cast<unsigned char>(spec.fill);
    if (spec.align == '<') {
        return writer.append_fill(spec.fill, padding) ? Error{} : Error{kOutputFull};
    }
    if (spec.align == '^' || mode == PaddingMode::plain || spec.fill == ' ') {
        const auto left = spec.align == '^' ? padding - padding / 2U : padding;
        if (length != 0U) std::memmove(writer.data + begin + left, writer.data + begin, length);
        std::memset(writer.data + begin, fill, left);
        const auto right = padding - left;
        if (right != 0U) std::memset(writer.data + begin + left + length, fill, right);
    } else {
        // BQLog preserves signs/prefixes even inside text arguments. Do not
        // replace this with conventional numeric-only zero padding.
        std::size_t ignore = 0U;
        std::size_t ignore_index = 0U;
        for (std::size_t index = 1U; index <= length; ++index) {
            const auto destination = begin + spec.width - index;
            const auto source = begin + length - index;
            const char ch = read_char(writer.data, source);
            if (ch == '+' || ch == '-') {
                writer.data[destination] = static_cast<std::byte>(fill);
                ignore = 1U;
                continue;
            }
            if (spec.fill == '0' && spec.prefix == '#' && (spec.type == 'b' || spec.type == 'x') &&
                (ch == 'b' || ch == 'B' || ch == 'x' || ch == 'X')) {
                if (source == begin) return kBadNumber;
                writer.data[destination] = static_cast<std::byte>(fill);
                ignore_index = source - 1U;
                ignore = 2U;
                continue;
            }
            writer.data[destination] = ignore == 2U && ignore_index == source
                                           ? static_cast<std::byte>(fill)
                                           : writer.data[source];
        }
        if (padding > ignore) std::memset(writer.data + begin + ignore, fill, padding - ignore);
    }
    writer.used += padding;
    return std::nullopt;
}
}  // namespace

FormatResult render_message_utf8(const std::byte* format, std::size_t format_size,
                                 const DecodedArg* args, std::size_t arg_count, std::byte* output,
                                 std::size_t capacity) noexcept {
    if (format == nullptr && format_size != 0U)
        return make_failure(FormatError::invalid_format_metadata, 0U);
    if (format_size > kMaxFormatBytes) return make_failure(FormatError::format_too_large, 0U);
    if (arg_count > kMaxArgCount || (args == nullptr && arg_count != 0U))
        return make_failure(FormatError::invalid_arguments, 0U);
    if (output == nullptr && capacity != 0U)
        return make_failure(FormatError::invalid_workspace, 0U);
    for (std::size_t index = 0; index < arg_count; ++index) {
        if (!valid_argument(args[index]))
            return make_failure(FormatError::invalid_arguments, 0U,
                                static_cast<std::uint8_t>(index));
    }
    CheckedTextWriter writer{output, std::min(capacity, kMaxMessageBytes)};
    if (arg_count == 0U) {
        return writer.append_bytes(format, format_size) ? make_success(writer.used)
                                                        : make_failure(kOutputFull, 0U);
    }
    std::size_t cursor = 0U;
    std::size_t argument = 0U;
    while (cursor < format_size) {
        const auto literal_begin = cursor;
        if (!copy_literal_run(format, format_size, cursor, writer))
            return make_failure(kOutputFull, literal_begin);
        if (cursor == format_size) break;
        const auto brace_offset = cursor;
        const char ch = read_char(format, cursor++);
        if (ch == '}') {
            if (cursor < format_size && read_char(format, cursor) == '}') ++cursor;
            if (!writer.append_char('}')) return make_failure(kOutputFull, brace_offset);
            continue;
        }
        std::size_t style_size = 0U;
        if (argument < arg_count) {
            const auto window = std::min<std::size_t>(20U, format_size - cursor);
            for (std::size_t index = 0U; index < window; ++index) {
                const char candidate = read_char(format, cursor + index);
                if (candidate == '{') break;
                if (candidate == '}') {
                    style_size = index + 1U;
                    break;
                }
            }
        }
        if (style_size == 0U) {
            if (!writer.append_char('{')) return make_failure(kOutputFull, brace_offset);
            continue;
        }
        const auto spec = parse_format_spec(format + cursor, style_size);
        cursor += spec.offset != 0U ? spec.offset + 1U : style_size;
        const auto begin = writer.used;
        PaddingMode mode = PaddingMode::compatibility;
        auto error = render_argument(args[argument], spec, writer, mode);
        if (!error) error = apply_padding(writer, begin, spec, mode);
        if (error) return make_failure(*error, brace_offset, static_cast<std::uint8_t>(argument));
        ++argument;
    }
    return make_success(writer.used);
}
namespace {
bool line_fixed_decimal(CheckedTextWriter& writer, std::uint32_t value, unsigned digits) noexcept {
    char buffer[10];
    if (digits == 0U || digits > sizeof(buffer)) return false;
    for (unsigned i = digits; i != 0U; --i) {
        buffer[i - 1U] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    }
    return value == 0U && writer.append_bytes(buffer, digits);
}

std::optional<FormatError> line_timestamp(CheckedTextWriter& writer, const RecordHeader& header,
                                          const TimeZoneConfig& zone,
                                          CalendarCache& cache) noexcept {
    if ((header.flags & kTimestampStatusMask) ==
        static_cast<std::uint8_t>(TimestampStatus::time_unavailable)) {
        constexpr std::string_view unavailable = "[time=unavailable]";
        return writer.append_bytes(unavailable.data(), unavailable.size())
                   ? std::nullopt
                   : std::optional{FormatError::text_output_limit_exceeded};
    }
    constexpr std::uint64_t billion = 1000000000ULL;
    const auto seconds = static_cast<std::int64_t>(header.time_value / billion);
    const auto local = seconds + static_cast<std::int64_t>(zone.offset_minutes) * 60;
    if (!cache.valid || cache.local_second != local ||
        cache.offset_minutes != zone.offset_minutes) {
        if (!std::in_range<std::time_t>(local)) return FormatError::time_conversion_failed;
        const auto time = static_cast<std::time_t>(local);
        std::tm calendar{};
        if (::gmtime_r(&time, &calendar) == nullptr || calendar.tm_year < -1900 ||
            calendar.tm_year > 8099)
            return FormatError::time_conversion_failed;
        std::array<char, 19> text{};
        CheckedTextWriter temporary{reinterpret_cast<std::byte*>(text.data()), text.size()};
        if (!line_fixed_decimal(temporary, static_cast<std::uint32_t>(calendar.tm_year + 1900),
                                4) ||
            !temporary.append_char('-') ||
            !line_fixed_decimal(temporary, static_cast<std::uint32_t>(calendar.tm_mon + 1), 2) ||
            !temporary.append_char('-') ||
            !line_fixed_decimal(temporary, static_cast<std::uint32_t>(calendar.tm_mday), 2) ||
            !temporary.append_char('T') ||
            !line_fixed_decimal(temporary, static_cast<std::uint32_t>(calendar.tm_hour), 2) ||
            !temporary.append_char(':') ||
            !line_fixed_decimal(temporary, static_cast<std::uint32_t>(calendar.tm_min), 2) ||
            !temporary.append_char(':') ||
            !line_fixed_decimal(temporary, static_cast<std::uint32_t>(calendar.tm_sec), 2))
            return FormatError::time_conversion_failed;
        cache.calendar = text;
        cache.local_second = local;
        cache.offset_minutes = zone.offset_minutes;
        cache.valid = true;
    }
    const int offset = zone.offset_minutes;
    const auto magnitude = static_cast<std::uint32_t>(offset < 0 ? -offset : offset);
    if (!writer.append_char('[') ||
        !writer.append_bytes(cache.calendar.data(), cache.calendar.size()) ||
        !writer.append_char('.') ||
        !line_fixed_decimal(writer, static_cast<std::uint32_t>(header.time_value % billion), 9) ||
        !writer.append_char(offset < 0 ? '-' : '+') ||
        !line_fixed_decimal(writer, magnitude / 60U, 2) || !writer.append_char(':') ||
        !line_fixed_decimal(writer, magnitude % 60U, 2) || !writer.append_char(']'))
        return FormatError::text_output_limit_exceeded;
    return std::nullopt;
}
}  // namespace

FormatResult compose_line(const ChannelCold& channel, const RecordHeader& header,
                          const TimeZoneConfig& time_zone, CalendarCache& cache,
                          const std::byte* message, std::size_t message_size, std::byte* output,
                          std::size_t capacity) noexcept {
    if (output == nullptr && capacity != 0U)
        return make_failure(FormatError::invalid_workspace, 0U);
    if ((message == nullptr && message_size != 0U) || time_zone.offset_minutes < -840 ||
        time_zone.offset_minutes > 840)
        return make_failure(FormatError::invalid_arguments, 0U);
    const auto status = header.flags & kTimestampStatusMask;
    if (header.category_id >= channel.dependencies.category_names.size() || header.level >= 6U ||
        (header.flags & static_cast<std::uint8_t>(~kKnownFlagMask)) != 0U || status == 3U ||
        (status == 2U && header.time_value != 0U) ||
        (status == 1U && !channel.dependencies.policy.fallback_timestamp_allowed()))
        return make_failure(FormatError::invalid_format_metadata, 0U);
    if (message_size > kMaxMessageBytes)
        return make_failure(FormatError::text_output_limit_exceeded, 0U);

    CheckedTextWriter writer{output, std::min(capacity, kMaxMessageBytes)};
    if (const auto error = line_timestamp(writer, header, time_zone, cache))
        return make_failure(*error, 0U);
    constexpr std::array<std::string_view, 6> levels{"TRACE",   "DEBUG", "INFO",
                                                     "WARNING", "ERROR", "FATAL"};
    const auto append = [&writer](std::string_view text) noexcept {
        return writer.append_bytes(text.data(), text.size());
    };
    const auto& logger = channel.dependencies.logger_name;
    const auto& category = channel.dependencies.category_names[header.category_id];
    char tid[20];
    const auto converted = std::to_chars(tid, tid + sizeof(tid), channel.os_thread_id);
    if (converted.ec != std::errc{}) return make_failure(FormatError::number_conversion_failed, 0U);
    if (!append(" [") || !append(levels[header.level]) || !append("] [") || !append(logger) ||
        !append("/") || !append(category) || !append("] [tid=") ||
        !writer.append_bytes(tid, static_cast<std::size_t>(converted.ptr - tid)) || !append("] ") ||
        !writer.append_bytes(message, message_size) || !writer.append_char('\n'))
        return make_failure(FormatError::text_output_limit_exceeded, 0U);
    return make_success(writer.used);
}
}  // namespace qlog::detail