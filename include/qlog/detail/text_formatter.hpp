#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "qlog/appender_config.hpp"
#include "qlog/detail/channel.hpp"
#include "qlog/detail/format_spec.hpp"
#include "qlog/detail/record_header.hpp"
#include "qlog/detail/record_types.hpp"

namespace qlog::detail {

struct CalendarCache final {
    bool valid{false};
    std::int64_t local_second{};
    std::int16_t offset_minutes{};
    std::array<char, 19> calendar{};
};

// Body formatting: preserve the existing six-argument API.
[[nodiscard]] FormatResult render_message_utf8(const std::byte* format, std::size_t format_size,
                                               const DecodedArg* args, std::size_t arg_count,
                                               std::byte* output, std::size_t capacity) noexcept;

// message and output must refer to disjoint storage. Failure returns size == 0.
[[nodiscard]] FormatResult compose_line(const ChannelCold& channel, const RecordHeader& header,
                                        const TimeZoneConfig& time_zone, CalendarCache& cache,
                                        const std::byte* message, std::size_t message_size,
                                        std::byte* output, std::size_t capacity) noexcept;

}  // namespace qlog::detail
