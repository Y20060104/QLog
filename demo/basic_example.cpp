#include "qlog/appender/appender_console.h"
#include "qlog/appender/appender_file_text.h"
#include "qlog/log/log_manager.h"
#include "qlog/serialization/entry_format.h"

#include <iostream>

using namespace qlog;
using namespace qlog::serialization;

static uint64_t create_log_with_appenders(const std::string& name)
{
    log_config cfg;
    cfg.name         = name;
    cfg.categories   = {"System", "Network", "AI"};
    cfg.level_bitmap = 0x3F; // 全部 6 个级别 (verbose ~ fatal)

    // ── Console Appender ──────────────────────────────────────────
    auto& layout = log_manager::instance().get_public_layout();
    auto console = std::make_unique<appender::appender_console>();
    appender::appender_config console_cfg{};
    console_cfg.type = appender::appender_type::console;
    console->init("console", console_cfg, &layout, &cfg.categories);
    cfg.appenders.push_back(std::move(console));

    // ── File Appender ─────────────────────────────────────────────
    auto file = std::make_unique<appender::appender_file_text>();
    appender::appender_config file_cfg{};
    file_cfg.type       = appender::appender_type::text_file;
    file_cfg.file_name  = name + ".log";
    file->init("file", file_cfg, &layout, &cfg.categories);
    cfg.appenders.push_back(std::move(file));

    return log_manager::instance().create_log(std::move(cfg));
}

int main()
{
    // 1. 创建日志实例（带 console + file appender）
    uint64_t log_id = create_log_with_appenders("basic_example");
    auto* log = log_manager::instance().get_log_by_id(log_id);

    if (!log)
    {
        std::cerr << "Failed to create log\n";
        return 1;
    }

    // 2. 写日志
    log->log(0, log_level::info,    "System started, version: {}",  "1.0.0");
    log->log(1, log_level::debug,   "TCP handshake with {}:{}",     "10.0.0.1", int32_t{443});
    log->log(1, log_level::warning, "Network latency high: {0}ms",  double{235.7});
    log->log(2, log_level::error,   "AI inference failed, code: {}", "E_TIMEOUT");
    log->log(0, log_level::fatal,   "Out of memory! ptr: {0}",      static_cast<const void*>(nullptr));

    // 3. 刷新并退出
    log_manager::instance().force_flush_all();
    log_manager::instance().destroy_log(log_id);

    std::cout << "Logs written to basic_example.log\n";
    return 0;
}
