#include "qlog/async_logger.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

#include <stdexcept>
#include <utility>
#include <atomic>

#include "qlog/detail/admission_clock.hpp"
#include "qlog/detail/channel.hpp"
#include "qlog/detail/filter_state.hpp"
#include "qlog/detail/format_hash.hpp"
#include "qlog/detail/producer_context.hpp"
#include "qlog/detail/producer_policy.hpp"

namespace qlog {
namespace {

void validate_logger_categories(const LoggerConfig& config) {
    const auto count = config.category_names.size();

    if (count == 0U || count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("invalid logger category count");
    }

    if (config.category_enabled.size() != count) {
        throw std::invalid_argument("logger category count mismatch");
    }

    for (const auto value : config.category_enabled) {
        if (value > 1U) {
            throw std::invalid_argument("logger category value must be 0 or 1");
        }
    }
}

LoggerConfig normalize_logger_config(const LoggerConfig& input) {
    // 先确保类别元数据成立，再按这个数量展开目标过滤表。
    validate_logger_categories(input);

    LoggerConfig result = input;

    if (result.appenders.empty()) {
        AppenderConfig console;
        console.name = "console";
        console.type = AppenderType::Console;
        console.enabled = true;
        console.filter.levels = 0x3FU;

        result.appenders.push_back(std::move(console));
    }

    const auto category_count = result.category_names.size();

    for (auto& appender : result.appenders) {
        if (appender.filter.category_enabled.empty()) {
            appender.filter.category_enabled.assign(category_count, std::uint8_t{1});
        }
    }

    return result;
}

void validate_appender_config(const AppenderConfig& config, std::size_t category_count) {
    switch (config.type) {
        case AppenderType::Console:
        case AppenderType::TextFile:
            break;

        default:
            throw std::invalid_argument("unknown appender type");
    }

    if ((config.filter.levels & ~std::uint32_t{0x3F}) != 0U) {
        throw std::invalid_argument("unknown appender level bits");
    }

    if (config.filter.category_enabled.size() != category_count) {
        throw std::invalid_argument("appender category count mismatch");
    }

    for (const auto value : config.filter.category_enabled) {
        if (value > 1U) {
            throw std::invalid_argument("appender category value must be 0 or 1");
        }
    }
}

void validate_logger_config(const LoggerConfig& config) {
    validate_logger_categories(config);

    const auto capacity = config.ring.capacity_bytes;
    const auto quota = config.ring.max_payload_bytes;

    if (capacity < 16U || capacity > (std::size_t{1} << 31U) ||
        (capacity & (capacity - 1U)) != 0U) {
        throw std::invalid_argument("invalid ring capacity");
    }

    if (quota < 32U || quota > capacity / 2U) {
        throw std::invalid_argument("invalid record payload quota");
    }

    const auto category_count = config.category_names.size();

    for (std::size_t i = 0; i < config.appenders.size(); ++i) {
        validate_appender_config(config.appenders[i], category_count);

        for (std::size_t j = 0; j < i; ++j) {
            if (config.appenders[i].name == config.appenders[j].name) {
                throw std::invalid_argument("duplicate appender name");
            }
        }
    }
}

std::uint32_t merge_appender_levels(const AppenderConfig* configs, std::size_t count) noexcept {
    std::uint32_t merged = 0U;

    for (std::size_t i = 0; i < count; ++i) {
        merged |= configs[i].filter.levels;
    }

    return merged;
}

LoggerConfig prepare_logger_config(const LoggerConfig& input) {
    auto result = normalize_logger_config(input);
    validate_logger_config(result);
    return result;
}

}  // namespace

namespace detail {
class FilterConfigAccess final {
    friend class qlog::AsyncLogger;

    static void set_category(FilterState& filter, std::uint32_t category_id,
                             bool enabled) noexcept {
        filter.publish_category(category_id, enabled);
    }
};
}  // namespace detail

class AsyncLogger::Impl final {
   public:
    explicit Impl(LoggerConfig prepared_config)
        : config(std::move(prepared_config)),
          logger_id(detail::allocate_logger_id()),
          filter(merge_appender_levels(config.appenders.data(), config.appenders.size()),
                 config.category_enabled.data(), config.category_enabled.size()),
          clock(detail::probe_admission_clock()),
          policy(detail::make_producer_policy(clock.has_fallback())),
          hash_dispatch(detail::FormatHashDispatch::automatic()),
          dependencies{filter, clock, policy, hash_dispatch, config.name, config.category_names}
    {}

    ~Impl() {
        auto* node = published_context.load(std::memory_order_relaxed);
        while (node != nullptr) {
            auto* next = node->published_next;
            delete node;
            node = next;
        }
    }

    LoggerConfig config;
    const std::uint64_t logger_id;
    detail::FilterState filter;
    const detail::ClockDescriptor clock;
    const detail::RecordValidationPolicy policy;
    const detail::FormatHashDispatch hash_dispatch;
    detail::ChannelDependencies dependencies;
    std::atomic<detail::ProducerContext*> published_context{nullptr};
    std::atomic<bool> registration_open{true};
};

AsyncLogger::AsyncLogger(const LoggerConfig& config)
    : impl_(std::make_unique<Impl>(prepare_logger_config(config))) {}

AsyncLogger::~AsyncLogger() = default;

bool AsyncLogger::set_category_enabled(std::uint32_t category_id, bool enabled) noexcept {
    if (category_id >= impl_->filter.category_count()) {
        return false;
    }

    detail::FilterConfigAccess::set_category(impl_->filter, category_id, enabled);
    return true;
}

void AsyncLogger::close_registration() {
    impl_->registration_open.store(false, std::memory_order_release);
}

detail::CallGate AsyncLogger::classify_call(std::uint32_t category_id,
                                            LogLevel level) const noexcept {
    if (!valid_level(level)) {
        return detail::CallGate::invalid_level;
    }

    const auto& filter = impl_->filter;
    if (category_id >= filter.category_count()) {
        return detail::CallGate::invalid_category;
    }

    if (!filter.allows_unchecked(category_id, static_cast<std::uint8_t>(level))) {
        return detail::CallGate::filtered;
    }

    return detail::CallGate::proceed;
}
detail::ContextResult AsyncLogger::acquire_context_for_thread() const noexcept {
    const detail::ContextRegistryView registry{impl_->logger_id, impl_->config.ring,
                                               impl_->dependencies, impl_->published_context,
                                               impl_->registration_open};
    return detail::acquire_thread_context(registry);
}
}  // namespace qlog