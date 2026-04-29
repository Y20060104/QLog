#include "qlog/serialization/log_filter.h"

#include <cassert>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

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

void test_default_all_enabled()
{
    std::cout << "\n[Test] default all enabled\n";
    log_filter f;
    // 默认所有 level + category 启用
    CHECK(f.is_enabled(0, log_level::verbose), "verbose enabled by default");
    CHECK(f.is_enabled(0, log_level::info), "info enabled by default");
    CHECK(f.is_enabled(127, log_level::fatal), "category 127 enabled");
}

void test_level_bitmap()
{
    std::cout << "\n[Test] level bitmap control\n";
    log_filter f;

    // 只开 info(2) 以上：bitmap = 0b00111100 = 0x3C
    f.set_level_bitmap(0x3C);
    CHECK(!f.is_enabled(0, log_level::verbose), "verbose disabled");
    CHECK(!f.is_enabled(0, log_level::debug), "debug disabled");
    CHECK(f.is_enabled(0, log_level::info), "info enabled");
    CHECK(f.is_enabled(0, log_level::warning), "warning enabled");
    CHECK(f.is_enabled(0, log_level::error), "error enabled");
    CHECK(f.is_enabled(0, log_level::fatal), "fatal enabled");
}

void test_category_control()
{
    std::cout << "\n[Test] category enable/disable\n";
    log_filter f;
    f.set_category_enabled(5, false);
    CHECK(!f.is_enabled(5, log_level::info), "category 5 disabled");
    CHECK(f.is_enabled(6, log_level::info), "category 6 still enabled");
    f.set_category_enabled(5, true);
    CHECK(f.is_enabled(5, log_level::info), "category 5 re-enabled");
}

void test_oob_category()
{
    std::cout << "\n[Test] out-of-bounds category\n";
    log_filter f;
    CHECK(!f.is_enabled(MAX_CATEGORIES, log_level::info), "oob returns false");
    CHECK(!f.is_enabled(0xFFFFFFFF, log_level::fatal), "max uint oob returns false");
}

void test_performance()
{
    std::cout << "\n[Test] is_enabled performance\n";
    log_filter f;

    constexpr int kIter = 10'000'000;
    volatile bool sink = false;

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIter; ++i)
    {
        sink ^= f.is_enabled(i % MAX_CATEGORIES, log_level::info);
    }
    auto t1 = std::chrono::steady_clock::now();
    (void)sink;

    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
    double per = static_cast<double>(ns) / kIter;
    std::cout << "  is_enabled: " << per << " ns/call\n";
    CHECK(per < 10.0, "is_enabled < 10ns per call");
}

void test_concurrent_read()
{
    std::cout << "\n[Test] concurrent reads (TSan check)\n";
    log_filter f;
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back(
            [&f]()
            {
                for (int j = 0; j < 100000; ++j)
                {
                    volatile bool v = f.is_enabled(j % MAX_CATEGORIES, log_level::info);
                    (void)v;
                }
            }
        );
    }
    for (auto& t : threads)
        t.join();
    std::cout << "  PASS: 8 concurrent readers, no data race\n";
}

int main()
{
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║   QLog M4 Log Filter Tests             ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";

    test_default_all_enabled();
    test_level_bitmap();
    test_category_control();
    test_oob_category();
    test_performance();
    test_concurrent_read();

    std::cout << "\n✅ All log_filter tests passed!\n";
    return 0;
}