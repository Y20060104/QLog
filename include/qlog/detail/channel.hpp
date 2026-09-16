#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include<utility>

#include "qlog/detail/admission_clock.hpp"
#include "qlog/detail/filter_state.hpp"
#include "qlog/detail/format_hash.hpp"
#include "qlog/detail/producer_identity.hpp"
#include "qlog/detail/record_types.hpp"
#include "spsc_ring_buffer.hpp"
namespace qlog::detail {
struct ChannelDependencies final {
   public:
    const FilterState& filter;
    const ClockDescriptor& clock;
    const RecordValidationPolicy& policy;
    const FormatHashDispatch& hash_dispatch;
    const std::string& logger_name;
    const std::vector<std::string>& category_names;
};

struct ChannelCold final {
    std::uint64_t logger_id;
    std::uint64_t producer_token;
    std::uint64_t os_thread_id{0};
    std::string thread_name;
    std::size_t payload_quota;
    ChannelDependencies dependencies;
};


class Channel final {
   public:
    Channel(ChannelCold cold_data, SpscRingBufferConfig ring_config)
        : cold_(std::move(cold_data)), ring_(ring_config) {}
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    Channel(Channel&&) = delete;
    Channel& operator=(Channel&&) = delete;
    ~Channel() = default;

    ChannelCold cold_;
    SpscRingBuffer ring_;
};
}  // namespace qlog::detail