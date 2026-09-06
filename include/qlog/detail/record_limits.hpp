#pragma once

#include <cstddef>
#include <cstdint>

namespace qlog::detail {
inline constexpr std::size_t kRecordHeaderBytes = 32U;
inline constexpr std::size_t kMaxFormatBytes = 8192U;
inline constexpr std::size_t kMaxArgCount = 32U;
inline constexpr std::uint8_t kTimestampStatusMask = 0x03U;
inline constexpr std::uint8_t kKnownFlagMask = 0x03U;

}  // namespace qlog::detail