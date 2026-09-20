#pragma once
#include <cstddef>
#include <cstdint>

namespace qlog {
enum class ThreadMode : std::uint8_t {
    async,
    independent,
};
struct BackendConfig {
    ThreadMode thread_mode{ThreadMode::async};
    std::uint32_t records_per_channel{64U};
    std::size_t bytes_per_channel{256U * 1024U};
};
}  // namespace qlog
