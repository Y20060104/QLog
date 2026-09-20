#include "qlog/logger_config.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <string>

#include "manual_worker_fixture.hpp"
#include "qlog/async_logger.hpp"
#include "qlog/management_result.hpp"

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::abort();
    }
}
template <typename F>
void expect_error(F&& operation, qlog::ConfigError reason, std::size_t index) {
    try {
        operation();
    } catch (const qlog::ConfigValidationError& error) {
        require(error.reason == reason, "configuration error reason");
        require(error.config_index == index, "configuration error index");
        return;
    }
    require(false, "expected configuration error");
}
template <typename F>
void check_appender_error(F&& modify, qlog::ConfigError reason) {
    qlog::LoggerConfig input;
    qlog::AppenderConfig first;
    first.name = "first";
    qlog::AppenderConfig second;
    second.name = "second";
    modify(second);
    input.appenders = {first, second};
    expect_error([&] { (void)qlog::prepare_logger_config(input); }, reason, 1U);
    expect_error([&] { (void)qlog::prepare_appender_configs(input.appenders.data(), 2U, 1U); },
                 reason, 1U);
    require(input.appenders[0].filter.category_enabled.empty(),
            "failure must not normalize caller input");
}
}  // namespace
int main() {
    qlog::test::install_manual_worker();
    using namespace qlog;
    constexpr auto no_index = std::numeric_limits<std::size_t>::max();
    LoggerConfig input;
    input.category_names = {"one", "two"};
    input.category_enabled = {1U, 0U};
    const auto prepared = prepare_logger_config(input);
    const auto reset = prepare_appender_configs(nullptr, 0U, 2U);
    require(input.appenders.empty(), "constructor preparation preserves caller");
    require(prepared.appenders.size() == 1U && reset.size() == 1U, "default console");
    require(reset[0].name == "console" && reset[0].type == AppenderType::Console,
            "default console identity");
    require(reset[0].filter.category_enabled == std::vector<std::uint8_t>({1U, 1U}),
            "appender categories independent of logger switch");
    require(prepared.appenders[0].filter.category_enabled == reset[0].filter.category_enabled,
            "constructor/reset default parity");
    expect_error([&] { (void)prepare_appender_configs(nullptr, 1U, 1U); },
                 ConfigError::invalid_pointer, no_index);
    expect_error([&] { (void)prepare_appender_configs(nullptr, 0U, 0U); },
                 ConfigError::category_count_mismatch, no_index);
    input.category_enabled = {1U};
    expect_error([&] { (void)prepare_logger_config(input); }, ConfigError::category_count_mismatch,
                 no_index);
    input = LoggerConfig{};
    input.backend.records_per_channel = 0U;
    expect_error([&] { (void)prepare_logger_config(input); }, ConfigError::invalid_backend_config,
                 no_index);
    input = LoggerConfig{};
    input.backend.thread_mode = static_cast<ThreadMode>(255U);
    expect_error([&] { (void)prepare_logger_config(input); }, ConfigError::invalid_backend_config,
                 no_index);

    check_appender_error([](auto& c) { c.name.clear(); }, ConfigError::empty_name);
    check_appender_error(
        [](auto& c) {
            c.name = "first";
            c.enabled = false;
        },
        ConfigError::duplicate_name);
    check_appender_error([](auto& c) { c.type = static_cast<AppenderType>(255U); },
                         ConfigError::invalid_type);
    check_appender_error([](auto& c) { c.filter.levels = 0x40U; }, ConfigError::invalid_level_bits);
    check_appender_error([](auto& c) { c.filter.category_enabled = {1U, 1U}; },
                         ConfigError::category_count_mismatch);
    check_appender_error([](auto& c) { c.filter.category_enabled = {2U}; },
                         ConfigError::invalid_category_value);
    check_appender_error([](auto& c) { c.text.batch_bytes = 65535U; },
                         ConfigError::invalid_output_config);
    check_appender_error([](auto& c) { c.text.flush_interval_us = 0U; },
                         ConfigError::invalid_output_config);
    check_appender_error([](auto& c) { c.text.time_zone.offset_minutes = 841; },
                         ConfigError::invalid_output_config);
    check_appender_error([](auto& c) { c.console.stream = static_cast<ConsoleStream>(255U); },
                         ConfigError::invalid_output_config);
    check_appender_error([](auto& c) { c.type = AppenderType::TextFile; },
                         ConfigError::invalid_path);
    check_appender_error(
        [](auto& c) {
            c.type = AppenderType::TextFile;
            c.file.path = std::string("a\0b", 3U);
        },
        ConfigError::invalid_path);
    check_appender_error(
        [](auto& c) {
            c.type = AppenderType::TextFile;
            c.file.path = "valid.log";
            c.file.retry_interval_us = 999U;
        },
        ConfigError::invalid_output_config);
    AppenderConfig console;
    console.name = "console";
    console.enabled = false;
    console.filter.levels = 2U;
    console.file.path = std::string("a\0b", 3U);
    console.file.retry_interval_us = 0U;
    auto list = prepare_appender_configs(&console, 1U, 1U);
    require(merge_appender_levels(list.data(), list.size()) == 2U, "disabled level merge");
    require(merge_appender_levels(nullptr, 0U) == 0U, "empty merge");
    require(list[0].file.path == console.file.path, "unselected file fields untouched");
    AppenderConfig file;
    file.name = "file";
    file.type = AppenderType::TextFile;
    file.file.path = "./qlog-config-migration-unused.log";
    file.console.stream = static_cast<ConsoleStream>(255U);
    list = prepare_appender_configs(&file, 1U, 1U);
    require(
        list[0].file.path == std::filesystem::absolute(file.file.path).lexically_normal().string(),
        "file path normalization");
    require(file.file.path == "./qlog-config-migration-unused.log", "caller path unchanged");
    file.file.path = "CMakeLists.txt/child.log";
    expect_error([&] { (void)prepare_appender_configs(&file, 1U, 1U); }, ConfigError::invalid_path,
                 0U);
    AsyncLogger logger(LoggerConfig{});
    require(logger.try_log(0U, LogLevel::info, "value={}", 42).accepted(),
            "constructor links new preparation and producer works");
    std::cout << "PASS: config parity, validation, rollback, paths, merge, constructor\n";
}
