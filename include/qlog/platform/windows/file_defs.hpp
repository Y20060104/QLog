#pragma once

#include <cstdint>

#if defined(_WIN32)
namespace qlog::platform {
using platform_file_handle = void*;

inline const platform_file_handle invalid_platform_file_handle =
    reinterpret_cast<void*>(static_cast<std::intptr_t>(-1));

inline bool is_platform_handle_valid(const platform_file_handle& file_handle) {
    return file_handle != invalid_platform_file_handle;
}

struct file_node_info {
    std::uint32_t volumn;
    std::uint32_t idx_high;
    std::uint32_t idx_low;

    std::uint64_t hash_code() const;

    bool operator==(const file_node_info& rhs) const {
        return volumn == rhs.volumn && idx_high == rhs.idx_high && idx_low == rhs.idx_low;
    }
};
}  // namespace qlog::platform
#endif
