#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <thread>
#include <vector>
#ifdef QLOG_DIAG_BQLOG
#include "bq_log/bq_log.h"
using logger_type = bq::log;
#else
#include "qlog/log.hpp"
using logger_type = qlog::log;
#endif

static void flush_all() {
#ifdef QLOG_DIAG_BQLOG
    bq::log::force_flush_all_logs();
#else
    qlog::log::force_flush_all();
#endif
}
int main(int argc, char** argv) {
    std::string kind = argc > 1 ? argv[1] : "burst";
    int groups = argc > 2 ? std::atoi(argv[2]) : 10000;
    int wait_us = argc > 3 ? std::atoi(argv[3]) : 2000;
    if (kind != "burst" || groups < 1 || wait_us < 0) {
        std::cerr << "Usage: QLogLatencyProbe burst [groups=10000] [wait_us=2000] [hp_threshold=1000] [csv_path]\n";
        return 2;
    }
    std::string config = R"(
      appenders_config.out.type=text_file
      appenders_config.out.levels=[all]
      appenders_config.out.file_name=benchmark_output/probe
      appenders_config.out.capacity_limit=1
    )";

    if (argc > 4) config += std::string("\nlog.high_perform_mode_freq_threshold_per_second=") + argv[4] + "\n";
    auto log = logger_type::create_log("write_thread_cost", config.c_str());
    if (!log.is_valid()) return 3;
    log.info("Logging iteration: {}, message: {}, double: {}", uint64_t(0), uint64_t(0), 0.0);
    flush_all();
    std::ofstream csv(argc > 5 ? argv[5] : "qlog-latency-batches.csv");
    if (!csv) { std::cerr << "cannot open batch CSV\n"; return 5; }
    std::uint64_t accepted=0;
    std::thread producer([&] {
      std::vector<double> times;
      times.reserve(static_cast<std::size_t>(groups));
      std::minstd_rand random(1);
      std::uniform_int_distribution<int64_t> pauses(wait_us, wait_us ? wait_us + 200 : 0);

      csv << "batch,mean_ns\n";
      for (int group=0; group<groups; ++group) {
        const double value = static_cast<double>(group) * 1.1;
        const auto begin=std::chrono::steady_clock::now();
        for (uint64_t i=0; i<20; ++i) accepted += static_cast<std::uint64_t>(log.info("Logging iteration: {}, message: {}, double: {}", static_cast<uint64_t>(group), i, value));
        const auto end=std::chrono::steady_clock::now();
        const double ns = static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin).count()) / 20.0;
        times.push_back(ns);
        if (wait_us) {
          const auto until=std::chrono::steady_clock::now()+std::chrono::microseconds(pauses(random));
          while (std::chrono::steady_clock::now()<until) {}
        }
      }
      for(std::size_t i=0;i<times.size();++i) {
        csv<<i<<','<<times[i];
        csv<<'\n';
      }
      std::sort(times.begin(),times.end());
      auto p=[&](double x){return times[std::min(times.size()-1,static_cast<std::size_t>(x*static_cast<double>(times.size())))];};
      std::cout<<std::fixed<<std::setprecision(2)<<"PROBE "<<kind<<" groups="<<groups<<" wait_us="<<wait_us<<" p50="<<p(.5)<<" p95="<<p(.95)<<" p99="<<p(.99)<<" max="<<times.back()<<'\n';
    });
    producer.join();
    flush_all();
    const auto attempted=static_cast<std::uint64_t>(groups)*20U;
    std::cout<<"accepted="<<accepted<<" attempted="<<attempted<<'\n';
    return accepted==attempted ? 0 : 4;
}
