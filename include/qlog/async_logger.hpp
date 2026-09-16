#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "qlog/log_format.hpp"
#include "qlog/log_result.hpp"
#include "qlog/appender_config.hpp"
#include "qlog/detail/argument_traits.hpp"
#include "qlog/detail/call_gate.hpp"
#include "qlog/detail/record_limits.hpp"
#include "qlog/detail/spsc_ring_buffer.hpp"
#include "qlog/log_level.hpp"
#include "qlog/detail/context_result.hpp"

namespace qlog {

struct LoggerConfig {
    std::string name;

    std::vector<std::string> category_names{"default"};
    std::vector<std::uint8_t> category_enabled{1U};

    // 这里只保存参数，不创建实际队列。
    detail::SpscRingBufferConfig ring;

    // 空输入在冷路径规范化为一个默认 Console 配置。
    std::vector<AppenderConfig> appenders;
};

class AsyncLogger final {
   public:
    explicit AsyncLogger(const LoggerConfig& config);
    ~AsyncLogger();

    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;
    AsyncLogger(AsyncLogger&&) = delete;
    AsyncLogger& operator=(AsyncLogger&&) = delete;

    [[nodiscard]] bool set_category_enabled(std::uint32_t category_id, bool enabled) noexcept;

    void close_registration();
    template <typename... Args>
        requires(sizeof...(Args) <= detail::kMaxArgCount) &&
                (detail::SupportedArgument<Args> && ...)
    [[nodiscard]] LogResult try_log(std::uint32_t category_id, LogLevel level, FormatView format,
                                    Args&&... args) const noexcept;

    [[nodiscard]] detail::CallGate classify_call(std::uint32_t category_id,
                                                 LogLevel level) const noexcept;

    [[nodiscard]] detail::ContextResult acquire_context_for_thread() const noexcept;

   private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace qlog