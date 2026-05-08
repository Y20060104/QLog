#include "qlog/appender/appender_file_text.h"
#include "qlog/log/log_manager.h"
#include "qlog/serialization/entry_format.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <numeric>
#include <string>
#include <thread>
#include <vector>

using namespace qlog;
using namespace qlog::serialization;
using clock_ns = std::chrono::high_resolution_clock;

// ── Helpers ────────────────────────────────────────────────────────────────

static void print_sep(const char* title)
{
    std::cout << "\n===================================================\n";
    std::cout << "  " << title << "\n";
    std::cout << "===================================================\n";
}

static double ns_to_mps(double ns_per_call)
{
    return 1e3 / ns_per_call;
}

// ── Benchmark 1: Hot-path latency (bare, no appenders) ─────────────────────

static void bench_hot_path_latency()
{
    print_sep("Bench 1: Hot-path latency (no appenders, no I/O)");

    log_config cfg;
    cfg.name           = "bench_hotpath";
    cfg.categories     = {"Bench"};
    cfg.lp_buffer_size = 16 * 1024 * 1024;

    const uint64_t id = log_manager::instance().create_log(std::move(cfg));
    auto* imp = log_manager::instance().get_log_by_id(id);

    constexpr int kWarmup = 5000;
    constexpr int kIter   = 1'000'000;
    volatile int64_t sink = 0;

    // Warmup
    for (int i = 0; i < kWarmup; ++i)
        imp->log(0, log_level::info, "warmup {}", int32_t{i});

    // Measure: bare log() hot path
    const auto t0 = clock_ns::now();
    for (int i = 0; i < kIter; ++i)
    {
        imp->log(0, log_level::info, "msg {} val {}", int32_t{i}, double{3.14});
        sink += i;
    }
    const auto t1 = clock_ns::now();

    const double ns_per_call = static_cast<double>((t1 - t0).count()) / kIter;
    std::cout << "  Bare log() hot path (int32 + double):\n";
    std::cout << "    Latency:    " << ns_per_call << " ns/call\n";
    std::cout << "    Throughput: " << ns_to_mps(ns_per_call) << " M entries/s\n";

    log_manager::instance().destroy_log(id);
    (void)sink;
}

// ── Benchmark 2: With layout + file appender (measure full pipeline overhead)

static void bench_file_appender_overhead()
{
    print_sep("Bench 2: Hot path + file appender (layout formatting)");

    log_config cfg;
    cfg.name       = "bench_file_overhead";
    cfg.categories = {"File"};

    auto& layout = log_manager::instance().get_public_layout();
    auto file = std::make_unique<appender::appender_file_text>();
    appender::appender_config fc{};
    fc.type      = appender::appender_type::text_file;
    fc.file_name = "bench_overhead.log";
    file->init("file", fc, &layout, &cfg.categories);
    cfg.appenders.push_back(std::move(file));

    const uint64_t id = log_manager::instance().create_log(std::move(cfg));
    auto* imp = log_manager::instance().get_log_by_id(id);

    constexpr int kWarmup = 2000;
    constexpr int kIter   = 500'000;

    for (int i = 0; i < kWarmup; ++i)
        imp->log(0, log_level::info, "warmup {}", int32_t{i});

    const auto t0 = clock_ns::now();
    for (int i = 0; i < kIter; ++i)
        imp->log(0, log_level::info, "msg {} val {}", int32_t{i}, double{3.14});
    const auto t1 = clock_ns::now();

    const double ns_per_call = static_cast<double>((t1 - t0).count()) / kIter;
    std::cout << "  log() + file appender buffered:\n";
    std::cout << "    Latency:    " << ns_per_call << " ns/call\n";
    std::cout << "    Throughput: " << ns_to_mps(ns_per_call) << " M entries/s\n";

    log_manager::instance().force_flush_all();
    log_manager::instance().destroy_log(id);
    // log file auto-cleaned by file appender rotation
}

// ── Benchmark 3: Multi-threaded throughput ─────────────────────────────────

static void bench_multi_threaded()
{
    print_sep("Bench 3: Multi-threaded throughput");

    std::vector<int> thread_counts = {1, 2, 4, 8};

    for (int n_threads : thread_counts)
    {
        log_config cfg;
        cfg.name           = "bench_mt";
        cfg.categories     = {"MT"};
        cfg.lp_buffer_size = 32 * 1024 * 1024;

        const uint64_t id = log_manager::instance().create_log(std::move(cfg));
        auto* imp = log_manager::instance().get_log_by_id(id);

        constexpr int kPerThread = 200'000;
        std::atomic<bool> start_gate{false};
        std::vector<std::thread> threads;
        std::vector<double> thread_times(n_threads);

        for (int t = 0; t < n_threads; ++t)
        {
            threads.emplace_back([&, t]()
            {
                while (!start_gate.load(std::memory_order_acquire)) {}
                const auto t0 = clock_ns::now();
                for (int i = 0; i < kPerThread; ++i)
                {
                    imp->log(0, log_level::info, "T{} msg {} val {}",
                             int32_t{t}, int32_t{i}, double{i * 1.5});
                }
                const auto t1 = clock_ns::now();
                thread_times[t] = static_cast<double>((t1 - t0).count());
            });
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        start_gate.store(true, std::memory_order_release);

        for (auto& th : threads)
            th.join();

        const double max_ns = *std::max_element(thread_times.begin(), thread_times.end());
        const double avg_ns = std::accumulate(thread_times.begin(), thread_times.end(), 0.0)
                            / n_threads;
        const size_t total = n_threads * kPerThread;
        const double total_mps = (total / 1e6) / (max_ns / 1e9);

        std::cout << "\n  [" << n_threads << " thread(s)] " << total << " total entries\n";
        std::cout << "    Per-call (avg): " << avg_ns / kPerThread << " ns\n";
        std::cout << "    Slowest thread: " << max_ns / kPerThread << " ns/call\n";
        std::cout << "    Throughput:     " << total_mps << " M entries/s\n";

        log_manager::instance().force_flush_all();
        log_manager::instance().destroy_log(id);
    }
}

// ── Benchmark 4: Parameter count impact ────────────────────────────────────

static void bench_param_counts()
{
    print_sep("Bench 4: Parameter count impact on hot path");

    log_config cfg;
    cfg.name           = "bench_params";
    cfg.categories     = {"Params"};
    cfg.lp_buffer_size = 16 * 1024 * 1024;

    const uint64_t id = log_manager::instance().create_log(std::move(cfg));
    auto* imp = log_manager::instance().get_log_by_id(id);

    constexpr int kIter = 200'000;

    struct param_case {
        const char* label;
        const char* fmt;
        int param_count;
    };

    param_case cases[] = {
        {"0 args (static msg)",          "static message no params",              0},
        {"1 arg (int32)",                "val {}",                                1},
        {"2 args (int32+double)",        "msg {} val {}",                         2},
        {"4 args (int+double+str+bool)", "a{} b{} c{} d{}",                      4},
    };

    const char* test_str = "hello_world";

    for (const auto& tc : cases)
    {
        // Warmup
        for (int i = 0; i < 500; ++i)
            imp->log(0, log_level::info, tc.fmt, int32_t{1}, double{2.0}, test_str, true);

        const auto t0 = clock_ns::now();
        for (int i = 0; i < kIter; ++i)
        {
            switch (tc.param_count)
            {
            case 0:
                imp->log(0, log_level::info, tc.fmt);
                break;
            case 1:
                imp->log(0, log_level::info, tc.fmt, int32_t{i});
                break;
            case 2:
                imp->log(0, log_level::info, tc.fmt, int32_t{i}, double{i * 1.5});
                break;
            case 4:
                imp->log(0, log_level::info, tc.fmt,
                         int32_t{i}, double{i * 1.5}, test_str, true);
                break;
            }
        }
        const auto t1 = clock_ns::now();

        const double ns_per_call = static_cast<double>((t1 - t0).count()) / kIter;
        std::cout << "  " << tc.label << ": " << ns_per_call << " ns/call\n";
    }

    log_manager::instance().destroy_log(id);
}

// ── Benchmark 5: End-to-end file I/O + force_flush ─────────────────────────

static void bench_file_flush()
{
    print_sep("Bench 5: File appender + force_flush bulk I/O");

    log_config cfg;
    cfg.name       = "bench_flush";
    cfg.categories = {"File"};

    auto& layout = log_manager::instance().get_public_layout();
    auto file = std::make_unique<appender::appender_file_text>();
    appender::appender_config fc{};
    fc.type      = appender::appender_type::text_file;
    fc.file_name = "bench_flush.log";
    file->init("file", fc, &layout, &cfg.categories);
    cfg.appenders.push_back(std::move(file));

    const uint64_t id = log_manager::instance().create_log(std::move(cfg));
    auto* imp = log_manager::instance().get_log_by_id(id);

    constexpr int kIter = 50'000;

    for (int i = 0; i < 500; ++i)
        imp->log(0, log_level::info, "warmup {}", int32_t{i});

    // Measure write phase
    const auto t0 = clock_ns::now();
    for (int i = 0; i < kIter; ++i)
        imp->log(0, log_level::info, "file msg {} val {}", int32_t{i}, double{i * 1.5});
    const auto t1 = clock_ns::now();

    // Measure flush phase
    const auto t_flush_start = clock_ns::now();
    log_manager::instance().force_flush_all();
    const auto t_flush_end = clock_ns::now();

    const double ns_per_call = static_cast<double>((t1 - t0).count()) / kIter;
    const double flush_ms = static_cast<double>((t_flush_end - t_flush_start).count()) / 1e6;

    std::cout << "  " << kIter << " entries written to file:\n";
    std::cout << "    Write latency: " << ns_per_call << " ns/call\n";
    std::cout << "    Bulk flush:    " << flush_ms << " ms\n";
    std::cout << "    Per-entry IO:  " << flush_ms * 1e6 / kIter << " ns (amortized)\n";

    log_manager::instance().destroy_log(id);
    // log file auto-cleaned by file appender rotation
}

// ── Benchmark 6: Concurrent write + periodic flush ─────────────────────────

static void bench_concurrent_flush()
{
    print_sep("Bench 6: Concurrent write + periodic flush (realistic)");

    log_config cfg;
    cfg.name           = "bench_cf";
    cfg.categories     = {"CF"};
    cfg.lp_buffer_size = 16 * 1024 * 1024;

    const uint64_t id = log_manager::instance().create_log(std::move(cfg));
    auto* imp = log_manager::instance().get_log_by_id(id);

    constexpr int kThreads    = 4;
    constexpr int kPerThread  = 100'000;
    constexpr int kFlushEvery = 25'000;
    std::atomic<bool> start_gate{false};
    std::atomic<int64_t> total_writes{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < kThreads; ++t)
    {
        threads.emplace_back([&, t]()
        {
            while (!start_gate.load(std::memory_order_acquire)) {}
            for (int i = 0; i < kPerThread; ++i)
            {
                imp->log(0, log_level::info, "T{} I{} data{}",
                         int32_t{t}, int32_t{i}, double{i * 0.5});
                if (i % kFlushEvery == 0 && t == 0)
                    log_manager::instance().try_flush_all();
                total_writes.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto t0 = clock_ns::now();
    start_gate.store(true, std::memory_order_release);

    for (auto& th : threads)
        th.join();
    const auto t1 = clock_ns::now();

    log_manager::instance().force_flush_all();

    const double total_ns = static_cast<double>((t1 - t0).count());
    const int64_t total = total_writes.load();
    const double ns_per_call = total_ns / total;
    const double total_mps = (total / 1e6) / (total_ns / 1e9);

    std::cout << "  " << kThreads << " threads x " << kPerThread << " = " << total << " entries\n";
    std::cout << "  Periodic try_flush every " << kFlushEvery << " entries\n";
    std::cout << "    Avg latency:  " << ns_per_call << " ns/call\n";
    std::cout << "    Throughput:   " << total_mps << " M entries/s\n";

    log_manager::instance().destroy_log(id);
}

// ── Main ───────────────────────────────────────────────────────────────────

int main()
{
    std::cout << "╔══════════════════════════════════════════════╗\n";
    std::cout << "║     QLog Stress / Performance Bench         ║\n";
    std::cout << "╚══════════════════════════════════════════════╝\n";

    bench_hot_path_latency();
    bench_file_appender_overhead();
    bench_multi_threaded();
    bench_param_counts();
    bench_file_flush();
    bench_concurrent_flush();

    std::cout << "\nAll stress benchmarks complete.\n";
    return 0;
}
