// Append this block at the END of src/text_formatter.cpp.
// This is a next-step reference, not yet part of the qlog library.
#include <ctime>
#include <string_view>
#include <utility>

namespace qlog::detail {
namespace {
bool line_fixed_decimal(CheckedTextWriter& writer, std::uint32_t value,
                        unsigned digits) noexcept {
    char buffer[10];
    if (digits == 0U || digits > sizeof(buffer)) return false;
    for (unsigned i = digits; i != 0U; --i) {
        buffer[i - 1U] = static_cast<char>('0' + value % 10U);
        value /= 10U;
    }
    return value == 0U && writer.append_bytes(buffer, digits);
}

std::optional<FormatError> line_timestamp(
    CheckedTextWriter& writer, const RecordHeader& header,
    const TimeZoneConfig& zone, CalendarCache& cache) noexcept {
    if ((header.flags & kTimestampStatusMask) ==
        static_cast<std::uint8_t>(TimestampStatus::time_unavailable)) {
        constexpr std::string_view unavailable = "[time=unavailable]";
        return writer.append_bytes(unavailable.data(), unavailable.size())
            ? std::nullopt : std::optional{FormatError::text_output_limit_exceeded};
    }
    constexpr std::uint64_t billion = 1000000000ULL;
    const auto seconds = static_cast<std::int64_t>(header.time_value / billion);
    const auto local = seconds + static_cast<std::int64_t>(zone.offset_minutes) * 60;
    if (!cache.valid || cache.local_second != local ||
        cache.offset_minutes != zone.offset_minutes) {
        if (!std::in_range<std::time_t>(local)) return FormatError::time_conversion_failed;
        const auto time = static_cast<std::time_t>(local);
        std::tm calendar{};
        if (::gmtime_r(&time, &calendar) == nullptr ||
            calendar.tm_year < -1900 || calendar.tm_year > 8099)
            return FormatError::time_conversion_failed;
        std::array<char, 19> text{};
        CheckedTextWriter temporary{reinterpret_cast<std::byte*>(text.data()), text.size()};
        if (!line_fixed_decimal(temporary, static_cast<std::uint32_t>(calendar.tm_year + 1900), 4) ||
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
        !line_fixed_decimal(writer, magnitude / 60U, 2) ||
        !writer.append_char(':') ||
        !line_fixed_decimal(writer, magnitude % 60U, 2) ||
        !writer.append_char(']'))
        return FormatError::text_output_limit_exceeded;
    return std::nullopt;
}
} // namespace

FormatResult compose_line(
    const ChannelCold& channel, const RecordHeader& header,
    const TimeZoneConfig& time_zone, CalendarCache& cache,
    const std::byte* message, std::size_t message_size,
    std::byte* output, std::size_t capacity) noexcept {
    if (output == nullptr && capacity != 0U)
        return make_failure(FormatError::invalid_workspace, 0U);
    if ((message == nullptr && message_size != 0U) ||
        time_zone.offset_minutes < -840 || time_zone.offset_minutes > 840)
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
    constexpr std::array<std::string_view, 6> levels{
        "TRACE", "DEBUG", "INFO", "WARNING", "ERROR", "FATAL"};
    const auto append = [&writer](std::string_view text) noexcept {
        return writer.append_bytes(text.data(), text.size());
    };
    const auto& logger = channel.dependencies.logger_name;
    const auto& category = channel.dependencies.category_names[header.category_id];
    char tid[20];
    const auto converted = std::to_chars(tid, tid + sizeof(tid), channel.os_thread_id);
    if (converted.ec != std::errc{}) return make_failure(FormatError::number_conversion_failed, 0U);
    if (!append(" [") || !append(levels[header.level]) || !append("] [") ||
        !append(logger) || !append("/") || !append(category) || !append("] [tid=") ||
        !writer.append_bytes(tid, static_cast<std::size_t>(converted.ptr - tid)) ||
        !append("] ") || !writer.append_bytes(message, message_size) || !writer.append_char('\n'))
        return make_failure(FormatError::text_output_limit_exceeded, 0U);
    return make_success(writer.used);
}
} // namespace qlog::detail
