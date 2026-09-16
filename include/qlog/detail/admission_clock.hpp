#pragma once
#include <time.h>

#include <cstdint>
#include <limits>

namespace qlog::detail {
enum class ClockSource : std::uint8_t { unavailable, realtime_coarse, realtime };

enum class ClockDomain : std::uint8_t { unix_epoch };
enum class ClockUnit : std::uint8_t { nanosecond };
enum class ClockCalibrationKind : std::uint8_t { none };
enum class ClockSamplingPoint : std::uint8_t { admission_timestamp };
enum class TimestampStatus : std::uint8_t {
    primary_valid = 0,
    fallback_valid = 1,
    time_unavailable = 2

};
struct ClockDescriptor final {
    // 冷描述版本，不是 Record ABI 版本。
    static constexpr std::uint32_t clock_descriptor_version = 1;
    static constexpr ClockDomain domain = ClockDomain::unix_epoch;
    static constexpr ClockUnit unit = ClockUnit::nanosecond;
    static constexpr ClockCalibrationKind calibration_kind = ClockCalibrationKind::none;
    static constexpr ClockSamplingPoint sampling_point = ClockSamplingPoint::admission_timestamp;

    ClockSource primary_source{ClockSource::unavailable};
    ClockSource fallback_source{ClockSource::unavailable};
    std::uint64_t primary_resolution_ns{};
    std::uint64_t fallback_resolution_ns{};

    [[nodiscard]] bool has_fallback() const noexcept {
        return fallback_source != ClockSource::unavailable;
    }
};
struct Timestamp final {
    std::uint64_t time_value{};
    std::uint8_t flags{static_cast<std::uint8_t>(TimestampStatus::time_unavailable)};
};
[[nodiscard]] ClockDescriptor probe_admission_clock() noexcept;

[[nodiscard]] Timestamp sample_admission_timestamp(const ClockDescriptor& descriptor) noexcept;
}  // namespace qlog::detail