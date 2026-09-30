#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace qlog::utility {
[[nodiscard]] constexpr bool checked_add(std::size_t lhs, std::size_t rhs,
                                         std::size_t& result) noexcept {
    if (rhs > std::numeric_limits<std::size_t>::max() - lhs) {
        return false;
    }
    result = rhs + lhs;
    return true;
}

[[nodiscard]] constexpr bool checked_mul(std::size_t lhs, std::size_t rhs,
                                         std::size_t& result) noexcept {
    if (lhs != 0U && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
        return false;
    }

    result = lhs * rhs;
    return true;
}

}  // namespace qlog::utility