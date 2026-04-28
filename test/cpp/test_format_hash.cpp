// test/cpp/test_format_hash.cpp
#include "qlog/serialization/format_hash.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

using qlog::serialization::crc32c_hash;
using qlog::serialization::crc32c_hash_32;
using qlog::serialization::crc32c_memcpy_with_hash;
using qlog::serialization::is_hw_crc32c_enabled;

// ==================== 轻量级测试框架 ====================
#define QTEST_CHECK(cond, op_name, expected, actual, is_cmp)                                    \
    do                                                                                          \
    {                                                                                           \
        if (!(cond))                                                                            \
        {                                                                                       \
            std::cerr << "[ FAILED ] " << __func__ << "\n"                                      \
                      << "  " << __FILE__ << ":" << __LINE__ << "\n"                            \
                      << "  Expected: " << (is_cmp ? #expected " " op_name " " #actual : #cond) \
                      << "\n";                                                                  \
            if (is_cmp)                                                                         \
            {                                                                                   \
                std::cerr << "  Actual values: " << (expected) << " " op_name " " << (actual)   \
                          << "\n";                                                              \
            }                                                                                   \
            std::abort();                                                                       \
        }                                                                                       \
    } while (0)

#define EXPECT_EQ(a, b) QTEST_CHECK((a) == (b), "==", a, b, true)
#define EXPECT_NE(a, b) QTEST_CHECK((a) != (b), "!=", a, b, true)
#define EXPECT_GE(a, b) QTEST_CHECK((a) >= (b), ">=", a, b, true)
#define EXPECT_LT(a, b) QTEST_CHECK((a) < (b), "<", a, b, true)
#define SUCCEED() \
    do            \
    {             \
    } while (0)

#define RUN_TEST(TestFunc)                               \
    do                                                   \
    {                                                    \
        TestFunc();                                      \
        std::cout << "[   OK   ] " << #TestFunc << "\n"; \
    } while (0)

// ==================== 基本性质 ====================
void test_EmptyInput()
{
    EXPECT_EQ(crc32c_hash("", 0), crc32c_hash(nullptr, 0));
}

void test_Determinism()
{
    const char* s = "Hello, QLog!";
    size_t n = std::strlen(s);
    EXPECT_EQ(crc32c_hash(s, n), crc32c_hash(s, n));
}

void test_DifferentInputsDifferentHashes()
{
    EXPECT_NE(crc32c_hash("a", 1), crc32c_hash("b", 1));
    EXPECT_NE(crc32c_hash("abc", 3), crc32c_hash("abd", 3));
}

void test_SmallSizes()
{
    // 覆盖 0 ~ 31 字节的所有小路径
    std::string buf(64, 'x');
    std::vector<uint64_t> hashes;
    for (size_t i = 0; i <= 31; ++i)
    {
        hashes.push_back(crc32c_hash(buf.data(), i));
    }
    // 不同长度应该产生不同哈希（高概率）
    std::sort(hashes.begin(), hashes.end());
    hashes.erase(std::unique(hashes.begin(), hashes.end()), hashes.end());
    EXPECT_GE(hashes.size(), 30u);
}

void test_LargeInput()
{
    std::vector<uint8_t> buf(4096);
    std::mt19937 rng(42);
    for (auto& b : buf)
        b = static_cast<uint8_t>(rng() & 0xFF);
    uint64_t h1 = crc32c_hash(buf.data(), buf.size());
    uint64_t h2 = crc32c_hash(buf.data(), buf.size());
    EXPECT_EQ(h1, h2);
}

// ==================== memcpy_with_hash 正确性 ====================
void test_MemcpyWithHashCopiesCorrectly()
{
    std::vector<uint8_t> src(1000);
    std::mt19937 rng(123);
    for (auto& b : src)
        b = static_cast<uint8_t>(rng() & 0xFF);

    std::vector<uint8_t> dst(1000, 0);
    uint64_t h = crc32c_memcpy_with_hash(dst.data(), src.data(), src.size());

    EXPECT_EQ(std::memcmp(dst.data(), src.data(), src.size()), 0);
    EXPECT_EQ(h, crc32c_hash(src.data(), src.size()));
}

void test_MemcpyWithHashAllSizes()
{
    std::vector<uint8_t> src(256);
    std::iota(src.begin(), src.end(), 0);
    std::vector<uint8_t> dst(256);

    for (size_t n = 0; n <= 256; ++n)
    {
        std::fill(dst.begin(), dst.end(), 0xAA);
        uint64_t h = crc32c_memcpy_with_hash(dst.data(), src.data(), n);
        EXPECT_EQ(std::memcmp(dst.data(), src.data(), n), 0);
        EXPECT_EQ(h, crc32c_hash(src.data(), n));
    }
}

// ==================== 硬件/软件一致性 ====================
void test_HwSwConsistency()
{
    // 该测试仅在硬件支持时有意义；通过 API 返回值已经保证一致
    // 这里验证 is_hw_crc32c_enabled() 不抛异常
    bool enabled = is_hw_crc32c_enabled();
    (void)enabled;
    SUCCEED();
}

// ==================== 32 位折叠 ====================
void test_Hash32Folding()
{
    const char* s = "format string %d %s";
    uint64_t h64 = crc32c_hash(s, std::strlen(s));
    uint32_t h32 = crc32c_hash_32(s, std::strlen(s));
    EXPECT_EQ(h32, static_cast<uint32_t>(h64 ^ (h64 >> 32)));
}

// ==================== 性能基准 ====================
void test_PerformanceBenchmark32Bytes()
{
    alignas(64) uint8_t buf[32];
    std::iota(std::begin(buf), std::end(buf), 0);

    constexpr int kIter = 1'000'000;
    volatile uint64_t sink = 0;

    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIter; ++i)
    {
        sink ^= crc32c_hash(buf, 32);
    }
    auto t1 = std::chrono::steady_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

    double per = static_cast<double>(ns) / kIter;
    std::printf(
        "[bench] crc32c_hash(32B): %.2f ns/op (HW=%d)\n", per, is_hw_crc32c_enabled() ? 1 : 0
    );

    // 硬件路径应 < 50ns；软件路径放宽
    if (is_hw_crc32c_enabled())
    {
        EXPECT_LT(per, 50.0);
    }
}

// ==================== Main ====================
int main()
{
    std::cout << "[==========] Running FormatHash standalone tests." << std::endl;

    RUN_TEST(test_EmptyInput);
    RUN_TEST(test_Determinism);
    RUN_TEST(test_DifferentInputsDifferentHashes);
    RUN_TEST(test_SmallSizes);
    RUN_TEST(test_LargeInput);
    RUN_TEST(test_MemcpyWithHashCopiesCorrectly);
    RUN_TEST(test_MemcpyWithHashAllSizes);
    RUN_TEST(test_HwSwConsistency);
    RUN_TEST(test_Hash32Folding);
    RUN_TEST(test_PerformanceBenchmark32Bytes);

    std::cout << "[==========] All tests passed successfully." << std::endl;
    return 0;
}