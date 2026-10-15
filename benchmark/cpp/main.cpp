// Adapted from Tencent/BqLog 086f2fd831af86fcfa40e90015f89626d498d3d7 benchmark/cpp/main.cpp.
// Normal workload and timing boundaries retained; QLog FAST is unavailable.
#if defined(WIN32)
#include <windows.h>
#endif
#include <stdio.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "multi_format_templates.h"
#include "qlog/log.hpp"
#include "write_thread_cost.h"

static qlog::log compressed_log = qlog::log::create_log("compress", R"(
		appenders_config.appender_3.type=compressed_file
		appenders_config.appender_3.levels=[all]
		appenders_config.appender_3.file_name= benchmark_output/compress
		appenders_config.appender_3.capacity_limit=1
    )");
static qlog::log compressed_enc_log = qlog::log::create_log("compress_enc", R"(
		appenders_config.appender_3.type=compressed_file
		appenders_config.appender_3.levels=[all]
		appenders_config.appender_3.file_name= benchmark_output/compress_enc
		appenders_config.appender_3.capacity_limit=1
        appenders_config.appender_3.pub_key=ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQCwv3QtDXB/fQN+FonyOHuS2uC6IZc16bfd6qQk4ykBOt3nTfBFcNr8ZWvvcf4H0hFkrpMtQ0AJO057GhVTQCCfnvfStSq2Yra+O5VGpI5Q6NLrUuVERimjNgwtxbXt3P8Nw87jEIJiY/8m2FUXhZEPwoA7t+2/953cNE1itJskJtojwaUlMN0dXBJxs4NP8MfBPPZQ5vNV8xgEf1SCQzQBAJsofy1kPHHqJNBXUBsNA44SP5H95JOz+r0oaNkYxT88Zk4tbk5N3hk5aXyZVp49OqhrXCPf5owDa4Lqk4UzVTk9EimxvtSuiUTzr7IJhHYy7jsGnSgq6dH0xlUfxKeX pippocao@PIPPOCAO-PC6
	)");
static qlog::log text_log = qlog::log::create_log("text", R"(
        appenders_config.appender_3.type=text_file
        appenders_config.appender_3.levels=[all]
        appenders_config.appender_3.file_name= benchmark_output/text
        appenders_config.appender_3.capacity_limit=1
    )");
static qlog::log compressed_log_ascii_utf8 = qlog::log::create_log("test_ascii_u8", R"(
		appenders_config.appender_3.type=compressed_file
		appenders_config.appender_3.levels=[all]
		appenders_config.appender_3.file_name= benchmark_output/test_ascii_u8
		appenders_config.appender_3.capacity_limit=1
	)");
static qlog::log compressed_log_ascii_utf16 = qlog::log::create_log("test_ascii_u16", R"(
		appenders_config.appender_3.type=compressed_file
		appenders_config.appender_3.levels=[all]
		appenders_config.appender_3.file_name= benchmark_output/test_ascii_u16
		appenders_config.appender_3.capacity_limit=1
    )");
static qlog::log compressed_log_chinese_utf16 = qlog::log::create_log("test_chinese_u16", R"(
		appenders_config.appender_3.type=compressed_file
		appenders_config.appender_3.levels=[all]
		appenders_config.appender_3.file_name= benchmark_output/test_chinese_u16
		appenders_config.appender_3.capacity_limit=1
    )");
static qlog::log compressed_log_mixed_utf16 = qlog::log::create_log("test_mixed_u16", R"(
		appenders_config.appender_3.type=compressed_file
		appenders_config.appender_3.levels=[all]
		appenders_config.appender_3.file_name= benchmark_output/test_mixed_u16
		appenders_config.appender_3.capacity_limit=1
	)");
static qlog::log compressed_log_multi_format = qlog::log::create_log("test_multi_format", R"(
		appenders_config.appender_3.type=compressed_file
		appenders_config.appender_3.levels=[all]
		appenders_config.appender_3.file_name= benchmark_output/test_multi_format
		appenders_config.appender_3.capacity_limit=1
	)");
static qlog::log write_thread_cost_log = qlog::log::create_log("write_thread_cost", R"(
        appenders_config.appender_3.type=text_file
        appenders_config.appender_3.levels=[all]
        appenders_config.appender_3.file_name= benchmark_output/write_thread_cost
        appenders_config.appender_3.capacity_limit=1
    )");

static std::string ascii_charset;
static std::u16string ascii_charset_u16;
static std::u16string chinese_charset_u16;
static std::u16string mixed_charset_u16;
static std::vector<std::tuple<size_t, size_t>> positions;
static constexpr size_t character_pool_size = 1024 * 1024 * 8;
static constexpr size_t logs_count = 2000000;

template <typename CHAR>
struct benchmark_string_view {
   public:
    using value_type = CHAR;
    using size_type = size_t;

   private:
    const CHAR* data_;
    size_t size_;

   public:
    benchmark_string_view(const CHAR* data, size_t size) : data_(data), size_(size) {}
    const CHAR* data() const {
        return data_;
    }
    size_t size() const {
        return size_;
    }
};

static size_t get_template_pool_size() {
#if defined(_MSC_VER)
    char* env_owned = nullptr;
    size_t env_length = 0;
    const auto env_error = _dupenv_s(&env_owned, &env_length, "BENCH_POOL_SIZE");
    std::string env_value = env_error == 0 && env_owned ? env_owned : "";
    std::free(env_owned);
    const char* env = env_value.c_str();
#else
    const char* env = getenv("BENCH_POOL_SIZE");
#endif
    if (env && env[0]) {
        long long v = atoll(env);
        if (v > 0 && (unsigned long long)v <= (unsigned long long)logs_count) {
            return (size_t)v;
        }
    }
    return 50000;
}

static void prepare_datas() {
    ascii_charset.resize(character_pool_size);
    ascii_charset_u16.resize(character_pool_size);
    chinese_charset_u16.resize(character_pool_size);
    mixed_charset_u16.resize(character_pool_size);
    for (size_t character_index = 0; character_index < character_pool_size; ++character_index) {
        ascii_charset[character_index] = (char)((character_index % 95) + 32);
        ascii_charset_u16[character_index] = (char16_t)((character_index % 95) + 32);
        chinese_charset_u16[character_index] = (char16_t)(0x4E00 + (character_index % 20902));
        if ((character_index % 200) < 100) {
            mixed_charset_u16[character_index] = (char16_t)((character_index % 95) + 32);
        } else {
            mixed_charset_u16[character_index] = (char16_t)(0x4E00 + (character_index % 20902));
        }
    }
    for (size_t log_index = 0; log_index < logs_count; ++log_index) {
        size_t start = (log_index * 9973) % (character_pool_size - 1);
        size_t max_size = (character_pool_size - 1 - start);
        size_t data_size = log_index % 1024;
        data_size = (std::min)(data_size, max_size);
        positions.push_back(std::make_tuple(start, data_size));
    }
}

void test_compress_ascii_utf8(int32_t thread_count) {
    std::cout << "============================================================" << std::endl;
    std::cout << "=========Begin Test Compressed File Log ASCII UTF8=========" << std::endl;
    qlog::log log_obj = qlog::log::get_log_by_name("test_ascii_u8");
    const size_t template_pool_ = get_template_pool_size();
    std::vector<std::thread> threads;
    threads.resize(thread_count);
    uint64_t start_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Now Begin, each thread will write 2000000 log entries, please wait the result..."
              << std::endl;
    for (int32_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads[thread_index] = std::thread([&log_obj, template_pool_]() {
            for (size_t log_index = 0; log_index < logs_count; ++log_index) {
                log_obj.info(benchmark_string_view<char>(
                    ascii_charset.data() + std::get<0>(positions[log_index % template_pool_]),
                    std::get<1>(positions[log_index % template_pool_])));
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

void test_compress_ascii_utf16(int32_t thread_count) {
    std::cout << "============================================================" << std::endl;
    std::cout << "=========Begin Test Compressed File Log ASCII UTF16=========" << std::endl;
    qlog::log log_obj = qlog::log::get_log_by_name("test_ascii_u16");
    const size_t template_pool_ = get_template_pool_size();
    std::vector<std::thread> threads;
    threads.resize(thread_count);
    uint64_t start_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Now Begin, each thread will write 2000000 log entries, please wait the result..."
              << std::endl;
    for (int32_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads[thread_index] = std::thread([&log_obj, template_pool_]() {
            for (size_t log_index = 0; log_index < logs_count; ++log_index) {
                log_obj.info(benchmark_string_view<char16_t>(
                    ascii_charset_u16.data() + std::get<0>(positions[log_index % template_pool_]),
                    std::get<1>(positions[log_index % template_pool_])));
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

void test_compress_chinese_utf16(int32_t thread_count) {
    std::cout << "============================================================" << std::endl;
    std::cout << "=========Begin Test Compressed File Log CHINESE UTF16=========" << std::endl;
    qlog::log log_obj = qlog::log::get_log_by_name("test_chinese_u16");
    const size_t template_pool_ = get_template_pool_size();
    std::vector<std::thread> threads;
    threads.resize(thread_count);
    uint64_t start_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Now Begin, each thread will write 2000000 log entries, please wait the result..."
              << std::endl;
    for (int32_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads[thread_index] = std::thread([&log_obj, template_pool_]() {
            for (size_t log_index = 0; log_index < logs_count; ++log_index) {
                log_obj.info(benchmark_string_view<char16_t>(
                    chinese_charset_u16.data() + std::get<0>(positions[log_index % template_pool_]),
                    std::get<1>(positions[log_index % template_pool_])));
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

void test_compress_mixed_utf16(int32_t thread_count) {
    std::cout << "============================================================" << std::endl;
    std::cout << "=========Begin Test Compressed File Log MIXED(ASCII + CHINESE) UTF16========="
              << std::endl;
    qlog::log log_obj = qlog::log::get_log_by_name("test_mixed_u16");
    const size_t template_pool_ = get_template_pool_size();
    std::vector<std::thread> threads;
    threads.resize(thread_count);
    uint64_t start_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Now Begin, each thread will write 2000000 log entries, please wait the result..."
              << std::endl;
    for (int32_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads[thread_index] = std::thread([&log_obj, template_pool_]() {
            for (size_t log_index = 0; log_index < logs_count; ++log_index) {
                log_obj.info(benchmark_string_view<char16_t>(
                    mixed_charset_u16.data() + std::get<0>(positions[log_index % template_pool_]),
                    std::get<1>(positions[log_index % template_pool_])));
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

void test_compress_multi_param(int32_t thread_count, write_thread_cost::write_mode mode) {
    std::cout << "============================================================" << std::endl;
    std::cout << "=========Begin Compressed File Log Test 1, 4 params========="
              << (mode == write_thread_cost::write_mode::fast ? " (BQ_LOG_FAST_INFO)"
                                                              : " (log.info)")
              << std::endl;
    qlog::log log_obj = qlog::log::get_log_by_name("compress");
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

void test_compress_enc_multi_param(int32_t thread_count, write_thread_cost::write_mode mode) {
    std::cout << "============================================================" << std::endl;
    std::cout << "=========Begin Encrypted Compressed File Log Test 1, 4 params========="
              << (mode == write_thread_cost::write_mode::fast ? " (BQ_LOG_FAST_INFO)"
                                                              : " (log.info)")
              << std::endl;
    qlog::log log_obj = qlog::log::get_log_by_name("compress_enc");
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

void test_compress_no_param(int32_t thread_count, write_thread_cost::write_mode mode) {
    std::cout << "============================================================" << std::endl;
    std::cout << "=========Begin Compressed File Log Test 3, no param========="
              << (mode == write_thread_cost::write_mode::fast ? " (BQ_LOG_FAST_INFO)"
                                                              : " (log.info)")
              << std::endl;
    qlog::log log_obj = qlog::log::get_log_by_name("compress");
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

void test_compress_enc_no_param(int32_t thread_count, write_thread_cost::write_mode mode) {
    std::cout << "============================================================" << std::endl;
    std::cout << "=========Begin Encrypted Compressed File Log Test 3, no param========="
              << (mode == write_thread_cost::write_mode::fast ? " (BQ_LOG_FAST_INFO)"
                                                              : " (log.info)")
              << std::endl;
    qlog::log log_obj = qlog::log::get_log_by_name("compress_enc");
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

void test_compress_multi_format_single(int32_t thread_count) {
    std::cout << "============================================================" << std::endl;
    std::cout << "======Begin Compressed Multi-Format Test: SINGLE template======" << std::endl;
    qlog::log log_obj = qlog::log::get_log_by_name("test_multi_format");
    std::vector<std::thread> threads;
    threads.resize(thread_count);
    uint64_t start_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Now Begin, each thread will write 2000000 log entries (1 template), please wait "
                 "the result..."
              << std::endl;
    for (int32_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads[thread_index] = std::thread([&log_obj]() {
            for (int32_t log_index = 0; log_index < 2000000; ++log_index) {
                bench_log_multi_format(log_obj, 0, log_index);  // always the same template
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

void test_compress_multi_format_roundrobin(int32_t thread_count) {
    std::cout << "============================================================" << std::endl;
    std::cout << "===Begin Compressed Multi-Format Test: ROUND-ROBIN (no locality)===" << std::endl;
    qlog::log log_obj = qlog::log::get_log_by_name("test_multi_format");
    // warm up: make sure all templates are already registered so we measure steady state
    for (int32_t format_index = 0; format_index < BENCH_MULTI_FORMAT_COUNT; ++format_index) {
        bench_log_multi_format(log_obj, format_index, 0);
    }
    qlog::log::force_flush_all();
    std::vector<std::thread> threads;
    threads.resize(thread_count);
    uint64_t start_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Now Begin, each thread will write 2000000 log entries ("
              << BENCH_MULTI_FORMAT_COUNT << " templates, round-robin), please wait the result..."
              << std::endl;
    for (int32_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads[thread_index] = std::thread([&log_obj]() {
            for (int32_t log_index = 0; log_index < 2000000; ++log_index) {
                bench_log_multi_format(log_obj, log_index % BENCH_MULTI_FORMAT_COUNT, log_index);
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

void test_compress_multi_format_hotwindow(int32_t thread_count) {
    std::cout << "============================================================" << std::endl;
    std::cout << "===Begin Compressed Multi-Format Test: HOT-WINDOW (locality)===" << std::endl;
    qlog::log log_obj = qlog::log::get_log_by_name("test_multi_format");
    // warm up: register all templates so the lookup structure is fully populated
    for (int32_t format_index = 0; format_index < BENCH_MULTI_FORMAT_COUNT; ++format_index) {
        bench_log_multi_format(log_obj, format_index, 0);
    }
    qlog::log::force_flush_all();
    constexpr int32_t hot_window_size = 8;
    constexpr int32_t burst_size = 64;
    std::vector<std::thread> threads;
    threads.resize(thread_count);
    uint64_t start_time =
        std::chrono::system_clock::now().time_since_epoch() / std::chrono::milliseconds(1);
    std::cout << "Now Begin, each thread will write 2000000 log entries ("
              << BENCH_MULTI_FORMAT_COUNT << " templates, hot-window W=" << hot_window_size
              << "), please wait the result..." << std::endl;
    for (int32_t thread_index = 0; thread_index < thread_count; ++thread_index) {
        threads[thread_index] = std::thread([&log_obj, hot_window_size, burst_size]() {
            int32_t window_base_index = 0;
            int32_t log_index = 0;
            while (log_index < 2000000) {
                for (int32_t burst_index = 0; burst_index < burst_size && log_index < 2000000;
                     ++burst_index, ++log_index) {
                    bench_log_multi_format(
                        log_obj, window_base_index + (log_index % hot_window_size), log_index);
                }
                window_base_index += hot_window_size;
                if (window_base_index > BENCH_MULTI_FORMAT_COUNT - hot_window_size) {
                    window_base_index = 0;
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

    qlog::log::force_flush_all();
    prepare_datas();
    test_compress_ascii_utf8(thread_count);
    test_compress_ascii_utf16(thread_count);
    test_compress_chinese_utf16(thread_count);
    test_compress_mixed_utf16(thread_count);

    // QLog currently supports only the normal log.info() path of the upstream benchmark.
    const write_thread_cost::write_mode modes[] = {write_thread_cost::write_mode::normal};
    for (const auto mode : modes) {
        test_compress_multi_param(thread_count, mode);
        test_compress_enc_multi_param(thread_count, mode);
        test_text_multi_param(thread_count, mode);
        test_compress_no_param(thread_count, mode);
        test_compress_enc_no_param(thread_count, mode);
        test_text_no_param(thread_count, mode);
    }

    // Multi-format tests: same total log volume as the single-format tests above,
    // but spread across BENCH_MULTI_FORMAT_COUNT distinct format templates so the
    // compressed appender's format-template lookup is exercised with a large
    // template set. Single-format (test_compress_multi_param above) vs these gives
    // the comparison.
    test_compress_multi_format_single(thread_count);
    test_compress_multi_format_roundrobin(thread_count);
    test_compress_multi_format_hotwindow(thread_count);

    write_thread_cost::test(thread_count);
    return 0;
}
