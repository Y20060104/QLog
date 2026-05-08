#if defined(WIN32)
#include <windows.h>
#endif
#include "qlog/appender/appender_file_text.h"
#include "qlog/log/log_manager.h"
#include "qlog/serialization/entry_format.h"

#include <chrono>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

using namespace qlog;
using namespace qlog::serialization;

// ── Global data (mirrors BqLog benchmark) ───────────────────────────────

static constexpr size_t character_pool_size = 1024 * 1024 * 8;
static constexpr size_t logs_count          = 2000000;

static std::vector<char> ascii_charset;

// (start, size) pairs for each log entry
static std::vector<std::pair<size_t, size_t>> positions;

// ── prepare_datas ────────────────────────────────────────────────────────

static void prepare_datas()
{
    ascii_charset.resize(character_pool_size);
    for (size_t i = 0; i < character_pool_size; ++i)
        ascii_charset[i] = (char)((i % 95) + 32);

    for (size_t i = 0; i < logs_count; ++i)
    {
        size_t start     = (i * 9973) % (character_pool_size - 1);
        size_t max_size  = (character_pool_size - 1 - start);
        size_t data_size = i % 1024;
        if (data_size > max_size) data_size = max_size;
        positions.emplace_back(start, data_size);
    }
}

// ── Helper: create a log with text_file appender ─────────────────────────

static uint64_t create_text_log(
    const std::string& name, const std::string& file_name,
    const std::vector<std::string>& categories)
{
    log_config cfg;
    cfg.name       = name;
    cfg.categories = categories;

    auto& layout = log_manager::instance().get_public_layout();
    auto  app    = std::make_unique<appender::appender_file_text>();
    appender::appender_config ac{};
    ac.type           = appender::appender_type::text_file;
    ac.file_name      = file_name;
    ac.capacity_limit = 1;
    app->init(name, ac, &layout, &cfg.categories);
    cfg.appenders.push_back(std::move(app));

    return log_manager::instance().create_log(std::move(cfg));
}

// ── Helper: create bare log (no appenders, hot-path only) ────────────────

static uint64_t create_bare_log(
    const std::string& name, const std::vector<std::string>& categories)
{
    log_config cfg;
    cfg.name       = name;
    cfg.categories = categories;
    return log_manager::instance().create_log(std::move(cfg));
}

// =========================================================================
// Test 1: Text file, ASCII string data (mirrors BqLog compress_ascii_utf8)
// =========================================================================

static void test_text_ascii_utf8(int32_t thread_count)
{
    std::cout << "============================================================" << std::endl;
    std::cout << "=========Begin Test TEXT File Log ASCII UTF8=========" << std::endl;

    uint64_t id  = create_text_log("text_ascii_u8", "benchmark_output/text_ascii_u8", {"ascii_u8"});
    auto*    imp = log_manager::instance().get_log_by_id(id);

    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    uint64_t start_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Now Begin, each thread will write 2000000 log entries, please wait the result..."
              << std::endl;

    for (int32_t idx = 0; idx < thread_count; ++idx)
    {
        threads.emplace_back([imp]()
        {
            for (size_t i = 0; i < logs_count; ++i)
            {
                auto [start, sz] = positions[i];
                std::string_view sv(ascii_charset.data() + start, sz);
                imp->log(0, log_level::info, "{}", sv);
            }
        });
    }

    for (auto& th : threads) th.join();
    log_manager::instance().force_flush_all();
    uint64_t flush_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Time Cost:" << (uint64_t)(flush_time - start_time) << std::endl;
    std::cout << "============================================================" << std::endl << std::endl;

    log_manager::instance().destroy_log(id);
}

// =========================================================================
// Test 2: Bare hot-path (no I/O), ASCII string data
// =========================================================================

static void test_bare_ascii_utf8(int32_t thread_count)
{
    std::cout << "============================================================" << std::endl;
    std::cout << "=========Begin Test BARE Hot-path ASCII UTF8=========" << std::endl;

    uint64_t id  = create_bare_log("bare_ascii_u8", {"bare_ascii_u8"});
    auto*    imp = log_manager::instance().get_log_by_id(id);

    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    uint64_t start_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Now Begin, each thread will write 2000000 log entries, please wait the result..."
              << std::endl;

    for (int32_t idx = 0; idx < thread_count; ++idx)
    {
        threads.emplace_back([imp]()
        {
            for (size_t i = 0; i < logs_count; ++i)
            {
                auto [start, sz] = positions[i];
                std::string_view sv(ascii_charset.data() + start, sz);
                imp->log(0, log_level::info, "{}", sv);
            }
        });
    }

    for (auto& th : threads) th.join();
    // No force_flush — just measure hot-path throughput
    uint64_t end_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Time Cost (hot-path only, no flush):" << (uint64_t)(end_time - start_time) << std::endl;
    std::cout << "============================================================" << std::endl << std::endl;

    log_manager::instance().destroy_log(id);
}

// =========================================================================
// Test 3: Text file, multi-param (4 params: int, int, float, bool)
// =========================================================================

static void test_text_multi_param(int32_t thread_count)
{
    std::cout << "============================================================" << std::endl;
    std::cout << "=========Begin TEXT File Log Test, 4 params=========" << std::endl;

    uint64_t id  = create_text_log("text_mp", "benchmark_output/text_mp", {"text_mp"});
    auto*    imp = log_manager::instance().get_log_by_id(id);

    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    uint64_t start_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Now Begin, each thread will write 2000000 log entries, please wait the result..."
              << std::endl;

    for (int32_t idx = 0; idx < thread_count; ++idx)
    {
        threads.emplace_back([idx, imp]()
        {
            for (int i = 0; i < 2000000; ++i)
            {
                imp->log(0, log_level::info, "idx:{}, num:{}, This test, {}, {}",
                         idx, i, 2.4232f, true);
            }
        });
    }

    for (auto& th : threads) th.join();
    log_manager::instance().force_flush_all();
    uint64_t flush_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Time Cost:" << (uint64_t)(flush_time - start_time) << std::endl;
    std::cout << "============================================================" << std::endl << std::endl;

    log_manager::instance().destroy_log(id);
}

// =========================================================================
// Test 4: Bare hot-path (no I/O), multi-param
// =========================================================================

static void test_bare_multi_param(int32_t thread_count)
{
    std::cout << "============================================================" << std::endl;
    std::cout << "=========Begin BARE Hot-path Test, 4 params=========" << std::endl;

    uint64_t id  = create_bare_log("bare_mp", {"bare_mp"});
    auto*    imp = log_manager::instance().get_log_by_id(id);

    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    uint64_t start_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Now Begin, each thread will write 2000000 log entries, please wait the result..."
              << std::endl;

    for (int32_t idx = 0; idx < thread_count; ++idx)
    {
        threads.emplace_back([idx, imp]()
        {
            for (int i = 0; i < 2000000; ++i)
            {
                imp->log(0, log_level::info, "idx:{}, num:{}, This test, {}, {}",
                         idx, i, 2.4232f, true);
            }
        });
    }

    for (auto& th : threads) th.join();
    uint64_t end_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Time Cost (hot-path only, no flush):" << (uint64_t)(end_time - start_time) << std::endl;
    std::cout << "============================================================" << std::endl << std::endl;

    log_manager::instance().destroy_log(id);
}

// =========================================================================
// Test 5: Text file, no param (static message)
// =========================================================================

static void test_text_no_param(int32_t thread_count)
{
    std::cout << "============================================================" << std::endl;
    std::cout << "=========Begin TEXT File Log Test, no param=========" << std::endl;

    uint64_t id  = create_text_log("text_np", "benchmark_output/text_np", {"text_np"});
    auto*    imp = log_manager::instance().get_log_by_id(id);

    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    uint64_t start_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Now Begin, each thread will write 2000000 log entries, please wait the result..."
              << std::endl;

    for (int32_t idx = 0; idx < thread_count; ++idx)
    {
        threads.emplace_back([imp]()
        {
            for (int i = 0; i < 2000000; ++i)
            {
                imp->log(0, log_level::info, "Empty Log, No Param");
            }
        });
    }

    for (auto& th : threads) th.join();
    log_manager::instance().force_flush_all();
    uint64_t flush_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Time Cost:" << (uint64_t)(flush_time - start_time) << std::endl;
    std::cout << "============================================================" << std::endl << std::endl;

    log_manager::instance().destroy_log(id);
}

// =========================================================================
// Test 6: Bare hot-path, no param
// =========================================================================

static void test_bare_no_param(int32_t thread_count)
{
    std::cout << "============================================================" << std::endl;
    std::cout << "=========Begin BARE Hot-path Test, no param=========" << std::endl;

    uint64_t id  = create_bare_log("bare_np", {"bare_np"});
    auto*    imp = log_manager::instance().get_log_by_id(id);

    std::vector<std::thread> threads;
    threads.reserve(thread_count);

    uint64_t start_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Now Begin, each thread will write 2000000 log entries, please wait the result..."
              << std::endl;

    for (int32_t idx = 0; idx < thread_count; ++idx)
    {
        threads.emplace_back([imp]()
        {
            for (int i = 0; i < 2000000; ++i)
            {
                imp->log(0, log_level::info, "Empty Log, No Param");
            }
        });
    }

    for (auto& th : threads) th.join();
    uint64_t end_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Time Cost (hot-path only, no flush):" << (uint64_t)(end_time - start_time) << std::endl;
    std::cout << "============================================================" << std::endl << std::endl;

    log_manager::instance().destroy_log(id);
}

// =========================================================================
// Main
// =========================================================================

int main()
{
#if defined(WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::cout << "Please input the number of threads which will write log simultaneously:" << std::endl;
    int32_t thread_count;
    std::cin >> thread_count;

    prepare_datas();

    // ASCII string data tests
    test_text_ascii_utf8(thread_count);
    test_bare_ascii_utf8(thread_count);

    // Multi-param tests (4 params: int, int, float, bool)
    test_text_multi_param(thread_count);
    test_bare_multi_param(thread_count);

    // No-param tests (static message)
    test_text_no_param(thread_count);
    test_bare_no_param(thread_count);

    return 0;
}
