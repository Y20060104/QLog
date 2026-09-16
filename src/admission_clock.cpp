#include "qlog/detail/admission_clock.hpp"

#include <time.h>

#include <cstdint>
#include <limits>

namespace qlog::detail {
namespace {
[[nodiscard]] bool epoch_ns_from_timespec(const timespec& input, std::uint64_t& output) noexcept {
    constexpr std::uint64_t billion = 1'000'000'000ULL;

    if (input.tv_sec < 0 || input.tv_nsec < 0 || input.tv_nsec >= 1'000'000'000L) {
        return false;
    }

    const auto seconds = static_cast<std::uint64_t>(input.tv_sec);

    const auto nanos = static_cast<std::uint64_t>(input.tv_nsec);

    if (seconds > (std::numeric_limits<std::uint64_t>::max() - nanos) / billion) {
        return false;
    }

    output = seconds * billion + nanos;
    return true;
}

bool clock_id_for(ClockSource source, clockid_t& out) noexcept {
    switch (source) {
        case ClockSource::realtime_coarse:
            out = CLOCK_REALTIME_COARSE;
            return true;
        case ClockSource::realtime:
            out = CLOCK_REALTIME;
            return true;
        case ClockSource::unavailable:
            return false;
        default:
            return false;
    }
}

bool probe_resolution_ns(ClockSource source, std::uint64_t& out) noexcept {
    clockid_t id{};
    if (!clock_id_for(source, id)) {
        return false;
    }

    timespec value{};
    if (::clock_getres(id, &value) != 0) {
        return false;
    }

    return epoch_ns_from_timespec(value, out);
}

[[nodiscard]] bool read_time_ns(ClockSource source, std::uint64_t& out) noexcept {
    clockid_t id{};
    if (!clock_id_for(source, id)) {
        return false;
    }

    timespec value{};
    if (::clock_gettime(id, &value) != 0) {
        return false;
    }

    return epoch_ns_from_timespec(value, out);
}

}  // namespace
[[nodiscard]] ClockDescriptor probe_admission_clock() noexcept {
    ClockDescriptor descriptor{};
    std::uint64_t coarse_resolution_ns{};
    std::uint64_t realtime_resolution_ns{};

    const bool coarse_ok = probe_resolution_ns(ClockSource::realtime_coarse, coarse_resolution_ns);
    const bool realtime_ok = probe_resolution_ns(ClockSource::realtime, realtime_resolution_ns);

    if (coarse_ok) {
        descriptor.primary_source = ClockSource::realtime_coarse;
        descriptor.primary_resolution_ns = coarse_resolution_ns;
        if (realtime_ok) {
            descriptor.fallback_source = ClockSource::realtime;
            descriptor.fallback_resolution_ns = realtime_resolution_ns;
        }
    } else if (realtime_ok) {
        descriptor.primary_source = ClockSource::realtime;
        descriptor.primary_resolution_ns = realtime_resolution_ns;
    }
    return descriptor;
}

[[nodiscard]] Timestamp sample_admission_timestamp(const ClockDescriptor& descriptor) noexcept {
    if (descriptor.primary_source == ClockSource::unavailable) {
        return Timestamp{};
    }

    std::uint64_t ns{};

    if (read_time_ns(descriptor.primary_source, ns)) {
        return Timestamp{ns, static_cast<std::uint8_t>(TimestampStatus::primary_valid)};
    }

    if (descriptor.has_fallback() && read_time_ns(descriptor.fallback_source, ns)) {
        return Timestamp{ns, static_cast<std::uint8_t>(TimestampStatus::fallback_valid)};
    }

    return Timestamp{};
}

}  // namespace qlog::detail