#pragma once

#include <array>
#include <cstdint>

#include "qlog/log_level.hpp"
#include "record_types.hpp"

namespace qlog::detail {
[[nodiscard]] constexpr RecordValidationPolicy make_producer_policy(bool has_fallback) noexcept {
    return RecordValidationPolicy{std::array<std::uint64_t, 4>{0x3FU, 0U, 0U, 0U}, has_fallback};
}
}  // namespace qlog::detail