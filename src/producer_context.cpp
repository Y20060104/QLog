#include "qlog/detail/producer_context.hpp"

#include <unistd.h>

#include <atomic>
#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>
namespace qlog::detail {
namespace {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<ProducerContext*>::is_always_lock_free);
std::atomic<std::uint64_t> next_logger_id{1};
std::atomic<std::uint64_t> next_producer_token{1};
struct Entry {
    std::uint64_t logger_id{0};
    ProducerContext* context{nullptr};
};

struct ThreadRegistry {
    std::uint64_t last_logger_id{0};
    ProducerContext* last_context{nullptr};
    std::uint64_t producer_token{0};
    std::vector<Entry> entries;
};

thread_local ThreadRegistry thread_registry;

std::unique_ptr<ProducerContext> make_context(const ContextRegistryView& view,
                                              std::uint64_t token) {
    const auto tid = static_cast<std::uint64_t>(::gettid());
    ChannelCold cold{
        view.logger_id,   token, tid, std::string{}, view.ring_config.max_payload_bytes,
        view.dependencies};
    return std::make_unique<ProducerContext>(std::move(cold), view.ring_config);
}
std::optional<std::uint64_t> take_id(std::atomic<std::uint64_t>& next) noexcept {
    std::uint64_t expected = next.load(std::memory_order_relaxed);
    if (expected == std::numeric_limits<std::uint64_t>::max()) {
        return std::nullopt;
    }

    while (expected != std::numeric_limits<std::uint64_t>::max()) {
        if (next.compare_exchange_weak(expected, expected + 1U, std::memory_order_relaxed,
                                       std::memory_order_relaxed)) {
            return expected;
        }
    }

    return std::nullopt;
}

ProducerContext* find_context(ThreadRegistry& registry, std::uint64_t logger_id) noexcept {
    assert(logger_id != 0);
    if (logger_id == registry.last_logger_id) {
        return registry.last_context;
    }
    for (auto& entry : registry.entries) {
        if (entry.logger_id == logger_id) {
            registry.last_logger_id = logger_id;
            registry.last_context = entry.context;
            return entry.context;
        }
    }

    return nullptr;
}

std::optional<std::size_t> prepare_tls_slot(ThreadRegistry& registry) {
    if (registry.entries.size() == registry.entries.max_size()) {
        return std::nullopt;
    }
    registry.entries.emplace_back();
    return registry.entries.size() - 1U;
}

bool ensure_thread_token(ThreadRegistry& registry) noexcept {
    if (registry.producer_token != 0) {
        return true;
    }
    const auto token = allocate_producer_token();
    if (!token) {
        return false;
    }
    registry.producer_token = *token;
    return true;
}

ProducerContext* publish_context(std::unique_ptr<ProducerContext>& node,
                                 std::atomic<ProducerContext*>& head) noexcept {
    auto* raw = node.get();
    assert(raw != nullptr);
    auto* expected = head.load(std::memory_order_relaxed);
    do {
        raw->published_next = expected;
    } while (!head.compare_exchange_weak(expected, raw, std::memory_order_release,
                                         std::memory_order_relaxed));
    return node.release();
}

void install_tls_slot(ThreadRegistry& registry, std::size_t index, std::uint64_t logger_id,
                      ProducerContext* context) noexcept {
    assert(index < registry.entries.size());
    assert(registry.entries[index].context == nullptr);
    assert(context != nullptr);

    registry.entries[index] = Entry{logger_id, context};
    registry.last_logger_id = logger_id;
    registry.last_context = context;
}

void rollback_tls_slot(ThreadRegistry& registry, [[maybe_unused]] std::size_t index) noexcept {
    assert(registry.entries.size() == index + 1U);
    assert(registry.entries[index].context == nullptr);
    registry.entries.pop_back();
}
}  // namespace
std::uint64_t allocate_logger_id() {
    const auto id = take_id(next_logger_id);
    if (!id) {
        throw std::overflow_error("QLog LoggerId exhausted");
    }
    return *id;
}

std::optional<std::uint64_t> allocate_producer_token() noexcept {
    return take_id(next_producer_token);
}

ContextResult acquire_thread_context(const ContextRegistryView& view) noexcept {
    auto& registry = thread_registry;
    if (auto* existing = find_context(registry, view.logger_id)) {
        return ContextResult::success(*existing);
    }

    if (!view.registration_open.load(std::memory_order_acquire)) {
        return ContextResult::failure(ContextError::registration_closed);
    }
    std::optional<std::size_t> slot;

    try {
        slot = prepare_tls_slot(registry);
        if (!slot) {
            return ContextResult::failure(ContextError::tls_capacity_exhausted);
        }
        if (!ensure_thread_token(registry)) {
            rollback_tls_slot(registry, *slot);
            return ContextResult::failure(ContextError::producer_token_exhausted);
        }

        auto node = make_context(view, registry.producer_token);
        auto* published = publish_context(node, view.head);
        install_tls_slot(registry, *slot, view.logger_id, published);
        return ContextResult::success(*published);
    } catch (const std::bad_alloc&) {
        if (slot) {
            rollback_tls_slot(registry, *slot);
        }
        return ContextResult::failure(ContextError::allocation_failed);
    }
}
}  // namespace qlog::detail