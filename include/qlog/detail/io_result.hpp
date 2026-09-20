#pragma once

#include <cstddef>
#include <cstdint>

namespace qlog::detail {
enum class FlushStatus : std::uint8_t {
    completed,
    incomplete,
    failed,
};
struct IoWriteResult final {
    std::size_t written;
    int error;
};
struct IoCallResult final {
    int error;
};
struct FlushResult final {
    FlushStatus status{FlushStatus::completed};
    std::size_t remaining_bytes{0};
    bool durability_uncertain{false};
};

inline constexpr std::uint32_t kRecoveryNamesPerVisit = 16U;
}  // namespace qlog::detail