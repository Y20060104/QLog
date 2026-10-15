#pragma once
#include <cstddef>
namespace qlog::utility {
inline constexpr std::size_t align_4(std::size_t n) {
    return (n + 3) & ~std::size_t(3);
}
inline constexpr std::size_t align_8(std::size_t n) {
    return (n + 7) & ~std::size_t(7);
}
}  // namespace qlog::utility
