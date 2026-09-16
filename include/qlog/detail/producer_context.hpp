#pragma once
#include <atomic>
#include <utility>

#include "qlog/detail/channel.hpp"
#include "qlog/detail/context_result.hpp"
#include "qlog/detail/producer_identity.hpp"

namespace qlog::detail {
class ProducerContext final {
   public:
    ProducerContext* published_next{nullptr};
    Channel channel_;
    ProducerContext(ChannelCold cold, SpscRingBufferConfig config)
        : channel_(std::move(cold), config) {}

    ProducerContext(const ProducerContext&) = delete;
    ProducerContext& operator=(const ProducerContext&) = delete;
    ProducerContext(ProducerContext&&) = delete;
    ProducerContext& operator=(ProducerContext&&) = delete;
};

struct ContextRegistryView final {
    std::uint64_t logger_id;
    SpscRingBufferConfig ring_config;
    ChannelDependencies dependencies;
    std::atomic<ProducerContext*>& head;
    const std::atomic<bool>& registration_open;
};

[[nodiscard]] ContextResult acquire_thread_context(const ContextRegistryView& registry) noexcept;
}  // namespace qlog::detail