#pragma once

#include <cstdint>

namespace qlog::utility {
inline std::uint32_t round_pow_of_two(std::uint32_t value) {
    value -= 1;
    value |= value >> 1;
    value |= value >> 2;
    value |= value >> 4;
    value |= value >> 8;
    value |= value >> 16;
    value += 1;
    return value;
}
}  // namespace qlog::utility