#include "qlog/async_logger.hpp"

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <stdexcept>
#include <utility>

#include "qlog/detail/admission_clock.hpp"
#include "qlog/detail/channel.hpp"
#include "qlog/detail/filter_config_access.hpp"
#include "qlog/detail/filter_state.hpp"
#include "qlog/detail/format_hash.hpp"
#include "qlog/detail/management_controller.hpp"
#include "qlog/detail/producer_context.hpp"
#include "qlog/detail/producer_diagnostics.hpp"
#include "qlog/detail/producer_policy.hpp"
#include "qlog/detail/worker_provider.hpp"
#include "qlog/logger_config.hpp"

namespace qlog {
class AsyncLogger::Impl final {
   public:
    explicit Impl(LoggerConfig prepared_config)
        : provider(detail::worker_provider()),
          config(std::move(prepared_config)),
          logger_id(detail::allocate_logger_id()),
          filter(qlog::merge_appender_levels(config.appenders.data(), config.appenders.size()),
                 config.category_enabled.data(), config.category_enabled.size()),
          clock(detail::probe_admission_clock()),
          policy(detail::make_producer_policy(clock.has_fallback())),
          hash_dispatch(detail::FormatHashDispatch::automatic()),
          dependencies{filter, clock, policy, hash_dispatch, config.name, config.category_names},
          management(mailbox, config.appenders, config.category_names.size(),
                     provider.console_gate()) {
        auto prepared = detail::prepare_reset(config.appenders.data(), config.appenders.size(),
                                              nullptr, 0, config.category_names.size(),
                                              ResetMode::recreate_all, provider.console_gate());
        session = std::make_unique<detail::BackendSession>(
            detail::SessionBindings{filter, published_context, mailbox, policy}, config.backend,
            std::move(prepared));
        worker = provider.attach(*session, config.backend.thread_mode);
        if (!worker) throw std::runtime_error("worker provider returned no attachment");
    }

    ~Impl() {
        auto* node = published_context.load(std::memory_order_relaxed);
        while (node != nullptr) {
            auto* next = node->published_next;
            delete node;
            node = next;
        }
    }

    detail::WorkerProvider& provider;
    LoggerConfig config;
    const std::uint64_t logger_id;
    detail::FilterState filter;
    const detail::ClockDescriptor clock;
    const detail::RecordValidationPolicy policy;
    const detail::FormatHashDispatch hash_dispatch;
    detail::ChannelDependencies dependencies;
    std::atomic<detail::ProducerContext*> published_context{nullptr};
    std::atomic<bool> registration_open{true};
#if QLOG_ENABLE_DIAGNOSTICS
    detail::ProducerDiagnostics diagnostics;
#endif
    detail::ControlMailbox mailbox;
    detail::ManagementController management;
    std::unique_ptr<detail::BackendSession> session;
    std::unique_ptr<detail::WorkerAttachment> worker;
    bool shutdown_done{false};
};

AsyncLogger::AsyncLogger(const LoggerConfig& config)
    : impl_(std::make_unique<Impl>(qlog::prepare_logger_config(config))) {}

AsyncLogger::~AsyncLogger() {
    (void)shutdown();
}

void AsyncLogger::notify_backend() const noexcept {
    impl_->worker->notify();
}

SubmitResult AsyncLogger::request_reset_appenders(const AppenderConfig* configs, std::size_t count,
                                                  ResetMode mode) {
    if (impl_->worker->failed()) impl_->management.stop_submissions();
    auto result = impl_->management.reset(configs, count, mode);
    if (result.status == SubmitStatus::submitted) notify_backend();
    return result;
}
SubmitResult AsyncLogger::request_flush_batches(FlushMode mode) {
    if (impl_->worker->failed()) impl_->management.stop_submissions();
    auto result = impl_->management.flush(mode);
    if (result.status == SubmitStatus::submitted) notify_backend();
    return result;
}
SubmitResult AsyncLogger::request_drain(FlushMode mode) {
    if (impl_->worker->failed()) impl_->management.stop_submissions();
    auto result = impl_->management.drain(mode);
    if (result.status == SubmitStatus::submitted) notify_backend();
    return result;
}
PollResult AsyncLogger::poll_management(std::uint64_t id) {
    if (impl_->worker->failed_and_released()) {
        impl_->management.stop_submissions();
        impl_->session->abandon_after_worker_failure();
    }
    return impl_->management.poll(id);
}
const ShutdownResult& AsyncLogger::shutdown(FlushMode mode) {
    auto& state = *impl_;
    if (state.shutdown_done) return state.session->shutdown_result();
    if (mode != FlushMode::buffered && mode != FlushMode::durable)
        throw std::invalid_argument("invalid shutdown flush mode");
    // Caller has stopped/joined all producers and other management callers.
    state.registration_open.store(false, std::memory_order_release);
    state.management.stop_submissions();
    if (state.mailbox.state.load(std::memory_order_acquire) == detail::CommandState::pending) {
        state.worker->notify();
        if (!state.worker->wait_for_command(state.mailbox))
            state.session->abandon_after_worker_failure();
    }
    if (state.mailbox.state.load(std::memory_order_acquire) == detail::CommandState::completed)
        (void)state.management.poll(state.mailbox.command->request_id);
    state.session->request_stop(mode);
    state.worker->notify();
    if (!state.worker->finish_stop()) state.session->abandon_after_worker_failure();
    if (!state.session->detached()) std::terminate();
    state.shutdown_done = true;
    return state.session->shutdown_result();
}

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

#if QLOG_ENABLE_DIAGNOSTICS
void AsyncLogger::record_call_result(const LogResult& result,
                                     std::size_t accepted_bytes) const noexcept {
    (void)accepted_bytes;  // 本步只做七项计数；保留既定桥接签名。
    auto& counters = impl_->diagnostics;
    const auto increment = [](std::atomic<std::uint64_t>& value) noexcept {
        value.fetch_add(1U, std::memory_order_relaxed);
    };
    increment(counters.call.calls);
    if (result.status() == LogStatus::filtered) {
        increment(counters.call.filtered);
        return;
    }
    if (result.accepted()) {
        increment(counters.admission.attempted);
        increment(counters.admission.accepted);
        return;
    }
    const auto* failure = result.failure();
    assert(failure != nullptr);
    if (failure == nullptr) {
        std::terminate();  // LogResult 内部不变量破坏，不伪造计数类别。
    }
    switch (failure->stage) {
        case FailureStage::validation:
        case FailureStage::context:
        case FailureStage::measure:
            increment(counters.call.rejected_pre_admission);
            return;
        case FailureStage::reserve:
        case FailureStage::encode:
            increment(counters.admission.attempted);
            if (failure->stage == FailureStage::reserve && result.status() == LogStatus::full) {
                increment(counters.admission.dropped_full);
            } else {
                increment(counters.admission.failed_after_attempt);
            }
            return;
    }
    std::terminate();
}
#endif

}  // namespace qlog