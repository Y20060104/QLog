#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "qlog/async_logger.hpp"

qlog::ManagementCompletion wait(qlog::AsyncLogger& logger, qlog::SubmitResult submitted) {
    if (submitted.status != qlog::SubmitStatus::submitted)
        throw std::runtime_error("management submission failed");
    for (;;) {
        auto result = logger.poll_management(submitted.request_id);
        if (result.status == qlog::PollStatus::completed) return std::move(*result.completion);
        if (result.status != qlog::PollStatus::pending) throw std::runtime_error("unknown request");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
int main(int argc, char** argv) try {
    qlog::LoggerConfig config;
    config.name = "demo";
    if (argc > 2 && std::string_view(argv[2]) == "independent")
        config.backend.thread_mode = qlog::ThreadMode::independent;
    qlog::AppenderConfig console;
    console.name = "console";
    qlog::AppenderConfig file;
    file.name = "file";
    file.type = qlog::AppenderType::TextFile;
    file.file.path = argc > 1 ? argv[1] : "qlog-demo.log";
    config.appenders = {console, file};
    qlog::AsyncLogger logger(config);  // built-in runtime starts automatically
    std::thread producer([&] {
        for (int i = 0; i < 10; ++i) {
            auto result = logger.try_log(0, qlog::LogLevel::info, "message={} hex={:#x}", i, i);
            if (!result.accepted()) std::cerr << "message not accepted\n";
        }
    });
    producer.join();  // drain and shutdown require stopped business users
    auto drained = wait(logger, logger.request_drain(qlog::FlushMode::buffered));
    if (drained.status != qlog::CompletionStatus::completed)
        throw std::runtime_error("drain failed");
    config.appenders[0].enabled = false;  // retain file and its pending batch
    auto reset = wait(
        logger, logger.request_reset_appenders(config.appenders.data(), config.appenders.size()));
    if (reset.status != qlog::CompletionStatus::applied) throw std::runtime_error("reset failed");
    if (!logger.try_log(0, qlog::LogLevel::info, "file-only after reset").accepted()) return 1;
    const auto& stopped = logger.shutdown(qlog::FlushMode::durable);
    // Console has no durable sync guarantee; inspect target errors explicitly.
    if (stopped.backend_failed || stopped.drain_incomplete || stopped.retired_io.lost_bytes)
        return 1;
    for (const auto& error : stopped.errors) {
        std::cerr << error.appender_name << ": errno=" << error.system_error << '\n';
        if (error.appender_name == "file") return 1;
    }
    return 0;
} catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
}
