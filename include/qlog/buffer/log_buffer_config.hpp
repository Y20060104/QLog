#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "qlog/buffer/log_buffer_defs.hpp"

namespace qlog::buffer {
struct LogBufferConfig {
    std::string log_name;
    std::vector<std::string> log_categories_name;

    std::uint32_t default_buffer_size = 1024U * 64U;
    bool need_recovery = false;
    LogMemoryPolicy policy = LogMemoryPolicy::block_when_full;
    std::uint64_t high_frequency_threshold_per_second = 1000;

    std::uint64_t calculate_check_sum() const;
};
}  // namespace qlog::buffer