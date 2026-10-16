// Adapted from Tencent/BqLog 086f2fd831af86fcfa40e90015f89626d498d3d7 benchmark/cpp/main.cpp.
// This branch retains the two normal Text cases and the write-thread-cost test.
// Messages and timing boundaries are unchanged; compressed and FAST cases are unavailable.
#if defined(WIN32)
#include <windows.h>
#endif
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "qlog/log.hpp"
#include "write_thread_cost.h"

static qlog::log text_log = qlog::log::create_log("text", R"(
        appenders_config.appender_3.type=text_file
        appenders_config.appender_3.levels=[all]
        appenders_config.appender_3.file_name= benchmark_output/text
        appenders_config.appender_3.capacity_limit=1
    )");
static qlog::log write_thread_cost_log = qlog::log::create_log("write_thread_cost", R"(
        appenders_config.appender_3.type=text_file
        appenders_config.appender_3.levels=[all]
        appenders_config.appender_3.file_name= benchmark_output/write_thread_cost
        appenders_config.appender_3.capacity_limit=1
    )");

void test_text_multi_param(int32_t thread_count, write_thread_cost::write_mode mode) {
    std::cout << "============================================================" << std::endl;
    std::cout << "============Begin Text File Log Test 2, 4 params============"
              << (mode == write_thread_cost::write_mode::fast ? " (BQ_LOG_FAST_INFO)"
                                                              : " (log.info)")
              << std::endl;
    qlog::log log_obj = qlog::log::get_log_by_name("text");
    std::vector<std::thread> threads;
    threads.resize(thread_count);
    uint64_t start_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Now Begin, each thread will write 2000000 log entries, please wait the result..."
              << std::endl;
    for (int32_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads[thread_index] = std::thread([thread_index, &log_obj, mode]() {
            if (mode == write_thread_cost::write_mode::fast) {
                throw std::logic_error(
                    "QLog FAST is not implemented; benchmark supports normal mode only");
            } else {
                for (int32_t log_index = 0; log_index < 2000000; ++log_index) {
                    log_obj.info("idx:{}, num:{}, This test, {}, {}", thread_index, log_index,
                                 2.4232f, true);
                }
            }
        });
    }
    for (int32_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads[thread_index].join();
    }
    qlog::log::force_flush_all();
    uint64_t flush_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Time Cost:" << (uint64_t)(flush_time - start_time) << std::endl;
    std::cout << "============================================================" << std::endl
              << std::endl;
}

void test_text_no_param(int32_t thread_count, write_thread_cost::write_mode mode) {
    std::cout << "============================================================" << std::endl;
    std::cout << "============Begin Text File Log Test 4, no param============"
              << (mode == write_thread_cost::write_mode::fast ? " (BQ_LOG_FAST_INFO)"
                                                              : " (log.info)")
              << std::endl;
    qlog::log log_obj = qlog::log::get_log_by_name("text");
    std::vector<std::thread> threads;
    threads.resize(thread_count);
    uint64_t start_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Now Begin, each thread will write 2000000 log entries, please wait the result..."
              << std::endl;
    for (int32_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads[thread_index] = std::thread([thread_index, &log_obj, mode]() {
            if (mode == write_thread_cost::write_mode::fast) {
                throw std::logic_error(
                    "QLog FAST is not implemented; benchmark supports normal mode only");
            } else {
                for (int32_t log_index = 0; log_index < 2000000; ++log_index) {
                    log_obj.info("Empty Log, No Param");
                }
            }
        });
    }
    for (int32_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads[thread_index].join();
    }
    qlog::log::force_flush_all();
    uint64_t flush_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Time Cost:" << (uint64_t)(flush_time - start_time) << std::endl;
    std::cout << "============================================================" << std::endl
              << std::endl;
}

int main() {
#if defined(WIN32)
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    std::cout << "Please input the number of threads which will write log simultaneously:"
              << std::endl;
    int32_t thread_count;
    std::cin >> thread_count;
    if (!std::cin || thread_count < 1) {
        return 1;
    }
    qlog::log::force_flush_all();
    const auto mode = write_thread_cost::write_mode::normal;
    test_text_multi_param(thread_count, mode);
    test_text_no_param(thread_count, mode);
    write_thread_cost::test(thread_count);
    return 0;
}
