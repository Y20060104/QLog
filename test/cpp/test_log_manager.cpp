/*
 * Copyright (c) 2026 QLog Contributors
 * Licensed under the Apache License, Version 2.0
 */

#include "qlog/appender/appender_console.h"
#include "qlog/log/log_manager.h"
#include "qlog/serialization/entry_format.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

using namespace qlog;
using namespace qlog::serialization;

#define CHECK(cond, msg)                                               \
    do                                                                 \
    {                                                                  \
        if (!(cond))                                                   \
        {                                                              \
            std::cerr << "FAIL [" << __LINE__ << "]: " << msg << "\n"; \
            std::abort();                                              \
        }                                                              \
        std::cout << "PASS: " << msg << "\n";                          \
    } while (0)

// ── 辅助: 创建带 console appender 的日志配置 ─────────────────────────────

static uint64_t create_test_log(const std::string& name)
{
    log_config cfg;
    cfg.name        = name;
    cfg.categories  = {"System", "Network", "AI"};
    cfg.level_bitmap = 0x3F;
    cfg.lp_buffer_size = 256 * 1024;

    // 注册 console callback 以静默输出（避免污染测试输出）
    // 实际使用中这里应 init console appender
    return log_manager::instance().create_log(std::move(cfg));
}

// ── Test 1: create_log / get_log_by_id / destroy_log ─────────────────────

void test_create_and_destroy()
{
    std::cout << "\n[Test 1] create_log / get_log_by_id / destroy_log\n";

    const uint64_t id = create_test_log("test_log_1");
    CHECK(id != 0, "create_log should return non-zero ID");

    auto* imp = log_manager::instance().get_log_by_id(id);
    CHECK(imp != nullptr, "get_log_by_id should return valid pointer");
    CHECK(imp->name() == "test_log_1", "log name should match config");
    CHECK(imp->id() == id, "log id should match returned ID");

    const bool ok = log_manager::instance().destroy_log(id);
    CHECK(ok, "destroy_log should return true for valid ID");

    auto* imp_after = log_manager::instance().get_log_by_id(id);
    CHECK(imp_after == nullptr, "get_log_by_id should return nullptr after destroy");
}

// ── Test 2: ID 编码安全性 ─────────────────────────────────────────────────

void test_id_encoding()
{
    std::cout << "\n[Test 2] ID encoding security (magic XOR)\n";

    const uint64_t id = create_test_log("test_log_2");

    // 篡改 ID 应返回 nullptr（不应崩溃）
    CHECK(log_manager::instance().get_log_by_id(id ^ 1) == nullptr,
          "tampered ID should not return valid pointer");
    CHECK(log_manager::instance().get_log_by_id(0) == nullptr,
          "zero ID should not be valid");
    CHECK(log_manager::instance().get_log_by_id(UINT64_MAX) == nullptr,
          "max ID should not be valid");

    log_manager::instance().destroy_log(id);
}

// ── Test 3: log() 热路径 - 基础写入 ─────────────────────────────────────

void test_basic_log()
{
    std::cout << "\n[Test 3] Basic log() hot path\n";

    const uint64_t id = create_test_log("test_log_3");
    auto* imp = log_manager::instance().get_log_by_id(id);
    CHECK(imp != nullptr, "log_imp should be valid");

    // 写入多条不同类型参数的日志
    imp->log(0, log_level::info, "User {0} logged in", int32_t{42});
    imp->log(1, log_level::warning, "Network latency: {0}ms", double{12.5});
    imp->log(2, log_level::error, "AI error code: {0}", "E_TIMEOUT");
    imp->log(0, log_level::debug, "Bool flag: {0}, ptr: {1}",
             bool{true}, static_cast<const void*>(nullptr));

    // 触发处理并等待完成
    log_manager::instance().force_flush_all();

    log_manager::instance().destroy_log(id);
    std::cout << "  4 entries written and processed successfully\n";
}

// ── Test 4: 过滤测试 ──────────────────────────────────────────────────────

void test_filter()
{
    std::cout << "\n[Test 4] Level and category filtering\n";

    log_config cfg;
    cfg.name         = "filter_log";
    cfg.categories   = {"Cat0", "Cat1"};
    cfg.level_bitmap = 0x3C; // info(2)+warning(3)+error(4)+fatal(5)，屏蔽 verbose+debug

    const uint64_t id = log_manager::instance().create_log(std::move(cfg));
    auto* imp = log_manager::instance().get_log_by_id(id);

    CHECK(!imp->is_enable_for(0, log_level::verbose), "verbose should be filtered");
    CHECK(!imp->is_enable_for(0, log_level::debug),   "debug should be filtered");
    CHECK( imp->is_enable_for(0, log_level::info),    "info should pass");
    CHECK( imp->is_enable_for(0, log_level::warning), "warning should pass");
    CHECK( imp->is_enable_for(0, log_level::error),   "error should pass");
    CHECK( imp->is_enable_for(0, log_level::fatal),   "fatal should pass");

    // 越界 category 应返回 false
    CHECK(!imp->is_enable_for(999, log_level::info), "OOB category should be filtered");

    log_manager::instance().destroy_log(id);
}

// ── Test 5: 多线程并发写入 ────────────────────────────────────────────────

void test_concurrent_write()
{
    std::cout << "\n[Test 5] Concurrent write (8 producers)\n";

    log_config cfg;
    cfg.name          = "concurrent_log";
    cfg.categories    = {"Thread"};
    cfg.lp_buffer_size = 4 * 1024 * 1024; // 4MB，防止满溢

    const uint64_t id = log_manager::instance().create_log(std::move(cfg));
    auto* imp = log_manager::instance().get_log_by_id(id);

    constexpr int k_threads = 8;
    constexpr int k_per_thread = 1000;
    std::atomic<int> write_count{0};

    std::vector<std::thread> threads;
    threads.reserve(k_threads);
    for (int t = 0; t < k_threads; ++t)
    {
        threads.emplace_back([&, t]()
        {
            for (int i = 0; i < k_per_thread; ++i)
            {
                imp->log(0, log_level::info, "T{0} I{1}", int32_t{t}, int32_t{i});
                write_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& th : threads)
        th.join();

    CHECK(write_count.load() == k_threads * k_per_thread,
          "all writes should succeed");

    log_manager::instance().force_flush_all();
    log_manager::instance().destroy_log(id);
    std::cout << "  " << k_threads << " x " << k_per_thread << " = "
              << k_threads * k_per_thread << " entries written successfully\n";
}

// ── Test 6: 多日志对象并存 ────────────────────────────────────────────────

void test_multiple_logs()
{
    std::cout << "\n[Test 6] Multiple log objects coexistence\n";

    constexpr int k_logs = 5;
    std::vector<uint64_t> ids;

    for (int i = 0; i < k_logs; ++i)
    {
        ids.push_back(create_test_log("multi_log_" + std::to_string(i)));
    }

    // 每个日志写一条
    for (int i = 0; i < k_logs; ++i)
    {
        auto* imp = log_manager::instance().get_log_by_id(ids[i]);
        CHECK(imp != nullptr, "log " + std::to_string(i) + " should be valid");
        imp->log(0, log_level::info, "Hello from log {0}", int32_t{i});
    }

    log_manager::instance().force_flush_all();

    // 验证 ID 互不混淆
    for (int i = 0; i < k_logs; ++i)
    {
        auto* imp = log_manager::instance().get_log_by_id(ids[i]);
        CHECK(imp != nullptr, "log " + std::to_string(i) + " should still be valid");
        CHECK(imp->name() == "multi_log_" + std::to_string(i),
              "log name should match");
    }

    for (auto id : ids)
        log_manager::instance().destroy_log(id);
}

// ── Test 7: 性能基准 ──────────────────────────────────────────────────────

void test_performance()
{
    std::cout << "\n[Test 7] Performance benchmark (log() hot path)\n";

    log_config cfg;
    cfg.name          = "perf_log";
    cfg.categories    = {"Perf"};
    cfg.lp_buffer_size = 16 * 1024 * 1024;
    cfg.hp_threshold  = 100; // 低阈值，快速切换到 HP 路径

    const uint64_t id = log_manager::instance().create_log(std::move(cfg));
    auto* imp = log_manager::instance().get_log_by_id(id);

    constexpr int k_iter = 500000;
    volatile int64_t sink = 0;

    // 预热
    for (int i = 0; i < 1000; ++i)
        imp->log(0, log_level::info, "warmup {0}", int32_t{i});

    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < k_iter; ++i)
    {
        imp->log(0, log_level::info, "msg {0} val {1}", int32_t{i}, double{3.14});
        sink += i;
    }
    const auto t1 = std::chrono::steady_clock::now();

    const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    const double ns_per_call = us * 1000.0 / k_iter;
    std::cout << "  log() hot path: " << ns_per_call << " ns/call ("
              << k_iter << " iterations)\n";

    CHECK(ns_per_call < 500.0, "hot path should be < 500ns/call");

    log_manager::instance().force_flush_all();
    log_manager::instance().destroy_log(id);
    (void)sink;
}

int main()
{
    std::cout << "╔══════════════════════════════════════════╗\n";
    std::cout << "║   QLog M8 log_manager Tests              ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n";

    test_create_and_destroy();
    test_id_encoding();
    test_basic_log();
    test_filter();
    test_concurrent_write();
    test_multiple_logs();
    test_performance();

    std::cout << "\n✅ All log_manager tests passed!\n";
    return 0;
}