#pragma once

#include <cstddef>
#include <cstdint>

#include "qlog/platform/macros.hpp"

namespace qlog::utility {
// Non-inline wrapper for the BQLog-compatible four-lane CRC32C hash.
std::uint64_t get_hash_64(const void* data, std::size_t size);

// dst and src cover len bytes and must not overlap; zero length returns zero.
std::uint64_t qlog_memcpy_with_hash(void* QLOG_RESTRICT dst, const void* QLOG_RESTRICT src,
                                    std::size_t len);
}  // namespace qlog::utility
