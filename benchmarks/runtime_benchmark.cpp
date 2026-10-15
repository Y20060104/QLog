#if defined(QLOG_BENCH_BQLOG)
#include "bq_log/bq_log.h"
using tested_log = bq::log;
using tested_level = bq::log_level;
#define PERF_STDCALL BQ_STDCALL
#else
#include "qlog/log.hpp"
using tested_log = qlog::log;
using tested_level = qlog::LogLevel;
#define PERF_STDCALL QLOG_STDCALL
#endif
#include <algorithm>
#include <atomic>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#if !defined(_WIN32)
#include <sys/resource.h>
#endif
namespace {
std::atomic<std::uint64_t> consumed{0}, output_bytes{0};
void PERF_STDCALL count_output(std::uint64_t id, std::int32_t, tested_level, const char*,
                               std::int32_t length) {
    if (id) {
        consumed.fetch_add(1, std::memory_order_relaxed);
        output_bytes.fetch_add(static_cast<std::uint64_t>(length), std::memory_order_relaxed);
    }
}
struct producer_result {
    std::uint64_t accepted = 0;
    std::vector<std::uint64_t> latency_ns;
};
double cpu_seconds() {
#if !defined(_WIN32)
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return static_cast<double>(usage.ru_utime.tv_sec + usage.ru_stime.tv_sec) +
           static_cast<double>(usage.ru_utime.tv_usec + usage.ru_stime.tv_usec) / 1000000.;
#else
    return 0;
#endif
}
std::uint64_t peak_rss_kib() {
#if !defined(_WIN32)
    rusage usage{};
    getrusage(RUSAGE_SELF, &usage);
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return 0;
#endif
}
std::uint64_t percentile(const std::vector<std::uint64_t>& values, double p) {
    if (values.empty()) {
        return 0;
    }
    return values[static_cast<std::size_t>(p * static_cast<double>(values.size() - 1))];
}
}  // namespace
int main(int argc, char** argv) {
    try {
        if (argc != 7) {
            throw std::runtime_error(
                "mode sink threads records_per_thread profile output_directory");
        }
        std::string mode = argv[1], sink = argv[2], profile = argv[5], directory = argv[6];
        int thread_count = std::stoi(argv[3]);
        std::uint64_t records = std::stoull(argv[4]);
        if (thread_count < 1 || records < 1) {
            throw std::runtime_error("positive workload required");
        }
        std::filesystem::create_directories(directory);
        std::string config = "log.thread_mode=" + mode +
                             "\nlog.buffer_size=65536\nlog.buffer_policy_when_full=block\n"
                             "log.high_perform_mode_freq_threshold_per_second=" +
                             (profile == "hp" ? std::string("1") : std::string("0")) + "\n";
        config += "appenders_config.target.type=" +
                  (sink == "text" ? std::string("text_file") : std::string("console")) + "\n";
        config += "appenders_config.target.levels=" +
                  (profile == "filtered" ? std::string("[error]") : std::string("[info]")) + "\n";
        if (sink == "text") {
            config += "appenders_config.target.file_name=" + directory +
                      "/out\n"
                      "appenders_config.target.enable_rolling_log_file=false\n"
                      "appenders_config.target.always_create_new_file=true\n"
                      "appenders_config.target.write_cache_size=65536\n";
        }
        tested_log::register_console_callback(&count_output);
        auto logger = tested_log::create_log("performance", config.c_str());
        if (!logger.is_valid()) {
            throw std::runtime_error("create failed");
        }
        std::barrier ready(thread_count + 1), start_gate(thread_count + 1);
        std::vector<producer_result> results(static_cast<std::size_t>(thread_count));
        std::vector<std::thread> workers;
        for (int t = 0; t < thread_count; ++t) {
            workers.emplace_back([&, t] {
                if (profile == "hp") {
                    (void)logger.info("warmup={}", t);
                    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
                }
                ready.arrive_and_wait();
                start_gate.arrive_and_wait();
                producer_result local;
                local.latency_ns.reserve(static_cast<std::size_t>(records / 256 + 1));
                for (std::uint64_t i = 0; i < records; ++i) {
                    bool sample = (i & 255U) == 0;
                    auto before = sample ? std::chrono::steady_clock::now()
                                         : std::chrono::steady_clock::time_point{};
                    bool accepted =
                        logger.info("record p={} seq={} a={} text={}", t, i, std::uint64_t{123456},
                                    "abcdefghijklmnopqrstuvwxyz012345");
                    if (sample) {
                        local.latency_ns.push_back(static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - before)
                                .count()));
                    }
                    local.accepted += accepted ? 1U : 0U;
                }
                results[static_cast<std::size_t>(t)] = std::move(local);
            });
        }
        ready.arrive_and_wait();
        logger.force_flush();
        consumed.store(0);
        output_bytes.store(0);
        double cpu_begin = cpu_seconds();
        auto begin = std::chrono::steady_clock::now();
        start_gate.arrive_and_wait();
        for (auto& worker : workers) {
            worker.join();
        }
        auto producer_end = std::chrono::steady_clock::now();
        logger.force_flush();
        auto drain_end = std::chrono::steady_clock::now();
        double cpu_used = cpu_seconds() - cpu_begin;
        std::uint64_t accepted = 0;
        std::vector<std::uint64_t> samples;
        for (const auto& result : results) {
            accepted += result.accepted;
            samples.insert(samples.end(), result.latency_ns.begin(), result.latency_ns.end());
        }
        std::sort(samples.begin(), samples.end());
        auto attempted = records * static_cast<std::uint64_t>(thread_count);
        auto delivered = consumed.load();
        std::uint64_t file_bytes = 0, text_records = 0;
        if (sink == "text") {
            // Count after the measured interval so validation does not enter throughput.
            for (const auto& entry : std::filesystem::directory_iterator(directory)) {
                if (entry.path().extension() == ".log") {
                    file_bytes += entry.file_size();
                }
            }
        }
        (void)text_records;
        if (sink != "text" && delivered != accepted) {
            throw std::runtime_error("accepted/output count mismatch");
        }
        if (profile != "filtered" && accepted != attempted) {
            throw std::runtime_error("block policy dropped record");
        }
        double producer_s = std::chrono::duration<double>(producer_end - begin).count();
        double total_s = std::chrono::duration<double>(drain_end - begin).count();
        double flush_s = std::chrono::duration<double>(drain_end - producer_end).count();
        std::cout << std::setprecision(12) << "BENCH_JSON {\"attempted\":" << attempted
                  << ",\"accepted\":" << accepted << ",\"rejected\":" << (attempted - accepted)
                  << ",\"callback_records\":" << delivered << ",\"producer_seconds\":" << producer_s
                  << ",\"drained_seconds\":" << total_s << ",\"flush_seconds\":" << flush_s
                  << ",\"producer_mps\":"
                  << (static_cast<double>(attempted) / producer_s / 1000000.)
                  << ",\"drained_mps\":" << (static_cast<double>(accepted) / total_s / 1000000.)
                  << ",\"p50_ns\":" << percentile(samples, .50)
                  << ",\"p99_ns\":" << percentile(samples, .99)
                  << ",\"p999_ns\":" << percentile(samples, .999)
                  << ",\"max_sample_ns\":" << (samples.empty() ? 0 : samples.back())
                  << ",\"sample_count\":" << samples.size() << ",\"cpu_seconds\":" << cpu_used
                  << ",\"cpu_cores_used\":" << (cpu_used / total_s)
                  << ",\"peak_rss_kib\":" << peak_rss_kib()
                  << ",\"callback_bytes\":" << output_bytes.load()
                  << ",\"file_bytes\":" << file_bytes << "}\n";
        tested_log::unregister_console_callback(&count_output);
#if defined(QLOG_BENCH_BQLOG)
        // Reference shutdown is performed by its global lifecycle; force_flush completed above.
#else
        qlog::log::uninit();
#endif
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "BENCH_FAIL " << e.what() << '\n';
        return 1;
    }
}
