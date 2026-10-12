#pragma once

#include <cstddef>
#include <cstdint>

namespace qlog::utility {
// Non-inline wrapper for the BQLog-compatible four-lane CRC32C hash.
std::uint64_t get_hash_64(const void* data, std::size_t size);
}  // namespace qlog::utility
