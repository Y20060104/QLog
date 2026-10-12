#pragma once

#include <sys/stat.h>

#include <cstdint>
#include <utility>

#if !defined(_WIN32)
namespace qlog::platform {
using platform_file_handle = std::int32_t;

inline constexpr platform_file_handle invalid_platform_file_handle = -1;

inline bool is_platform_handle_valid(const platform_file_handle& file_handle) {
    return file_handle >= 0;
}

struct file_node_info {
    decltype(std::declval<struct stat>().st_dev) dev;  // CR:decltype 是什么？ declval呢？
    decltype(std::declval<struct stat>().st_ino) ino;

    std::uint64_t hash_code() const;

    bool operator==(const file_node_info& rhs) const {
        return dev == rhs.dev && ino == rhs.ino;
    }
};

}  // namespace qlog::platform
#endif
