#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

#if defined(_WIN32)
#include "qlog/platform/windows/file_defs.hpp"
#else
#include "qlog/platform/posix/file_defs.hpp"
#endif

namespace qlog::platform {
enum class file_open_mode_enum {
    read = 1,
    write = 1 << 1,
    read_write = 3,
    auto_create = 1 << 2,
    exclusive = 1 << 3
};

enum class file_seek_option : std::uint8_t { current, begin, end };

inline file_open_mode_enum operator|(file_open_mode_enum lhs, file_open_mode_enum rhs) {
    return static_cast<file_open_mode_enum>(static_cast<std::int32_t>(lhs) |
                                            static_cast<std::int32_t>(rhs));
}

inline file_open_mode_enum operator&(file_open_mode_enum lhs, file_open_mode_enum rhs) {
    return static_cast<file_open_mode_enum>(static_cast<std::int32_t>(lhs) &
                                            static_cast<std::int32_t>(rhs));
}

}  // namespace qlog::platform

namespace std {
template <>
struct hash<qlog::platform::file_node_info> {
    std::size_t operator()(const qlog::platform::file_node_info& node_info) const;
};
}  // namespace std
