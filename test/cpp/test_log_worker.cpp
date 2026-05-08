/*
 * Copyright (c) 2026 QLog Contributors
 * Licensed under the Apache License, Version 2.0
 */

#include "qlog/worker/log_worker.h"

#include <atomic>
#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>

using namespace qlog;

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

// ── Test 1: 基础 start/stop ──────────────────────────────────────────────

void test_start_stop()
{
    std::cout << "\n[Test 1] Start / Stop\n";
    log_worker worker;

    std::atomic<int> call_count{0};
    worker.start([&](bool) { call_count.fetch_add(1, std::memory_order_relaxed); });

    CHECK(worker.is_running(), "worker should be running after start");

    // 等待至少 1 个自然周期（66ms）
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    const int count_before_stop = call_count.load();
    CHECK(count_before_stop >= 1, "worker should have processed at least one cycle");

    worker.stop();
    CHECK(!worker.is_running(), "worker should not be running after stop");
}

// ── Test 2: awake() 立即唤醒 ─────────────────────────────────────────────

void test_awake()
{
    std::cout << "\n[Test 2] Awake (immediate wakeup)\n";
    log_worker worker;

    std::atomic<int> call_count{0};
    worker.start([&](bool) { call_count.fetch_add(1, std::memory_order_relaxed); });

    const int before = call_count.load();
    worker.awake();

    // 唤醒后应在短时间（<<66ms）内触发一次处理
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK(call_count.load() > before, "awake() should trigger a processing cycle");

    worker.stop();
}

// ── Test 3: awake_and_wait_join() 同步等待 ───────────────────────────────

void test_awake_and_wait_join()
{
    std::cout << "\n[Test 3] awake_and_wait_join (force flush synchronization)\n";
    log_worker worker;

    std::atomic<uint64_t> last_epoch{0};
    worker.start([&](bool force_flush)
    {
        last_epoch.store(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count(),
            std::memory_order_release
        );
        (void)force_flush;
    });

    const uint64_t before_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();

    worker.awake_and_wait_join();

    // 调用返回时，至少完成了一个完整的处理周期
    CHECK(last_epoch.load(std::memory_order_acquire) >= before_ms,
          "awake_and_wait_join should block until processing is complete");

    worker.stop();
}

// ── Test 4: force_flush 参数传递 ─────────────────────────────────────────

void test_force_flush_parameter()
{
    std::cout << "\n[Test 4] Force flush parameter passing\n";
    log_worker worker;

    std::atomic<bool> received_force_flush{false};
    worker.start([&](bool force_flush)
    {
        if (force_flush)
            received_force_flush.store(true, std::memory_order_release);
    });

    worker.awake_and_wait_join(); // 内部设置 force_flush=true

    CHECK(received_force_flush.load(std::memory_order_acquire),
          "awake_and_wait_join should deliver force_flush=true to callback");

    worker.stop();
}

// ── Test 5: 66ms 周期性唤醒 ──────────────────────────────────────────────

void test_periodic_cycle()
{
    std::cout << "\n[Test 5] Periodic 66ms cycle\n";
    log_worker worker;

    std::atomic<int> count{0};
    worker.start([&](bool) { count.fetch_add(1, std::memory_order_relaxed); });

    // 在 300ms 内应有 ~4 个自然周期（300/66 ≈ 4.5）
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const int n = count.load();

    CHECK(n >= 3, "300ms should contain at least 3 processing cycles (66ms each)");
    CHECK(n <= 6, "300ms should not contain more than 6 cycles (sanity check)");
    std::cout << "  Cycles in 300ms: " << n << " (expected ~4)\n";

    worker.stop();
}

// ── Test 6: 重复 start/stop ───────────────────────────────────────────────

void test_restart()
{
    std::cout << "\n[Test 6] Restart worker\n";
    log_worker worker;

    std::atomic<int> count{0};
    auto cb = [&](bool) { count.fetch_add(1, std::memory_order_relaxed); };

    for (int i = 0; i < 3; ++i)
    {
        count.store(0);
        worker.start(cb);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        worker.stop();
        CHECK(count.load() >= 1, "restart " + std::to_string(i) + ": at least one cycle");
    }
}

int main()
{
    std::cout << "╔══════════════════════════════════════════╗\n";
    std::cout << "║   QLog M7 log_worker Tests               ║\n";
    std::cout << "╚══════════════════════════════════════════╝\n";

    test_start_stop();
    test_awake();
    test_awake_and_wait_join();
    test_force_flush_parameter();
    test_periodic_cycle();
    test_restart();

    std::cout << "\n✅ All log_worker tests passed!\n";
    return 0;
}