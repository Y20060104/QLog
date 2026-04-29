#include "qlog/serialization/entry_format.h"
#include "qlog/serialization/serializer.h"

#include <cassert>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>

using namespace qlog::serialization;

struct TestResult
{
    int passed = 0, failed = 0;
};
static TestResult g_res;

#define CHECK(cond, msg)                                                 \
    do                                                                   \
    {                                                                    \
        if (!(cond))                                                     \
        {                                                                \
            std::cerr << "  FAIL [" << __LINE__ << "]: " << msg << "\n"; \
            g_res.failed++;                                              \
        }                                                                \
        else                                                             \
        {                                                                \
            std::cout << "  PASS: " << msg << "\n";                      \
            g_res.passed++;                                              \
        }                                                                \
    } while (0)

// ── Test 1: 所有基础类型 roundtrip ──────────────────────────────────────────
void test_all_numeric_roundtrip()
{
    std::cout << "\n[Test 1] All numeric types roundtrip\n";
    uint8_t buf[32];

    // int8
    {
        size_t n = serializer::write(buf, int8_t{-42});
        CHECK(n == 2, "int8 size == 2");
        CHECK(buf[0] == uint8_t(param_type::type_int8), "int8 tag");
        CHECK(serializer::read_int8(buf) == int8_t{-42}, "int8 roundtrip");
    }

    // uint8
    {
        size_t n = serializer::write(buf, uint8_t{200});
        CHECK(n == 2, "uint8 size == 2");
        CHECK(serializer::read_uint8(buf) == uint8_t{200}, "uint8 roundtrip");
    }

    // int16
    {
        size_t n = serializer::write(buf, int16_t{-30000});
        CHECK(n == 3, "int16 size == 3");
        CHECK(serializer::read_int16(buf) == int16_t{-30000}, "int16 roundtrip");
    }

    // uint16
    {
        size_t n = serializer::write(buf, uint16_t{60000});
        CHECK(serializer::read_uint16(buf) == uint16_t{60000}, "uint16 roundtrip");
    }

    // int32
    {
        size_t n = serializer::write(buf, int32_t{-12345678});
        CHECK(n == 5, "int32 size == 5");
        CHECK(serializer::read_int32(buf) == int32_t{-12345678}, "int32 roundtrip");
    }

    // uint32
    {
        serializer::write(buf, uint32_t{0xDEADBEEF});
        CHECK(serializer::read_uint32(buf) == uint32_t{0xDEADBEEF}, "uint32 roundtrip");
    }

    // int64
    {
        size_t n = serializer::write(buf, int64_t{-9000000000LL});
        CHECK(n == 9, "int64 size == 9");
        CHECK(serializer::read_int64(buf) == int64_t{-9000000000LL}, "int64 roundtrip");
    }

    // uint64
    {
        serializer::write(buf, uint64_t{0xCAFEBABEDEADBEEFULL});
        CHECK(serializer::read_uint64(buf) == uint64_t{0xCAFEBABEDEADBEEFULL}, "uint64 roundtrip");
    }

    // float
    {
        serializer::write(buf, float{3.14159f});
        CHECK(serializer::read_float(buf) == float{3.14159f}, "float roundtrip");
    }

    // double
    {
        serializer::write(buf, double{2.718281828});
        CHECK(serializer::read_double(buf) == double{2.718281828}, "double roundtrip");
    }

    // bool true/false
    {
        serializer::write(buf, true);
        CHECK(serializer::read_bool(buf) == true, "bool true roundtrip");
        serializer::write(buf, false);
        CHECK(serializer::read_bool(buf) == false, "bool false roundtrip");
    }

    // pointer
    {
        int dummy = 0;
        void* ptr = &dummy;
        serializer::write(buf, ptr);
        CHECK(
            serializer::read_pointer(buf) == reinterpret_cast<uintptr_t>(ptr), "pointer roundtrip"
        );
    }
}

// ── Test 2: type tag 值对齐 BqLog ─────────────────────────────────────────
void test_type_tag_values()
{
    std::cout << "\n[Test 2] Type tag values (BqLog alignment)\n";
    CHECK(uint8_t(param_type::type_null) == 0, "null tag == 0");
    CHECK(uint8_t(param_type::type_string_utf8) == 1, "string_utf8 tag == 1");
    CHECK(uint8_t(param_type::type_bool) == 4, "bool tag == 4");
    CHECK(uint8_t(param_type::type_pointer) == 5, "pointer tag == 5");
    CHECK(uint8_t(param_type::type_float) == 6, "float tag == 6");
    CHECK(uint8_t(param_type::type_double) == 7, "double tag == 7");
    CHECK(uint8_t(param_type::type_int8) == 8, "int8 tag == 8");
    CHECK(uint8_t(param_type::type_int32) == 12, "int32 tag == 12");
    CHECK(uint8_t(param_type::type_int64) == 14, "int64 tag == 14");
}

// ── Test 3: string roundtrip + 截断 ──────────────────────────────────────
void test_string_roundtrip()
{
    std::cout << "\n[Test 3] String roundtrip and truncation\n";
    uint8_t buf[MAX_STRING_BYTES + 64];

    // 普通字符串
    {
        const std::string_view sv = "Hello, QLog!";
        size_t n = serializer::write(buf, sv);
        CHECK(n == 1 + 4 + sv.size(), "string encoded size");
        auto [rsv, consumed] = serializer::read_string(buf);
        CHECK(rsv == sv, "string content match");
        CHECK(consumed == n, "consumed == n");
    }

    // 空字符串
    {
        size_t n = serializer::write(buf, std::string_view{});
        CHECK(n == 1 + 4, "empty string size == 5");
        auto [rsv, consumed] = serializer::read_string(buf);
        CHECK(rsv.empty(), "empty string content");
    }

    // null ptr
    {
        size_t n = serializer::write(buf, static_cast<const char*>(nullptr));
        CHECK(n == 1 + 4, "null ptr treated as empty");
    }

    // 截断
    {
        std::string big(MAX_STRING_BYTES + 100, 'X');
        size_t n = serializer::write(buf, std::string_view{big});
        CHECK(n == 1 + 4 + MAX_STRING_BYTES, "truncated to MAX_STRING_BYTES");
        auto [rsv, _] = serializer::read_string(buf);
        CHECK(rsv.size() == MAX_STRING_BYTES, "read back truncated size");
    }
}

// ── Test 4: skip_param 迭代器逻辑 ────────────────────────────────────────
void test_skip_param()
{
    std::cout << "\n[Test 4] skip_param iterator\n";
    uint8_t buf[256];
    size_t offset = 0;

    // 写入多个参数
    offset += serializer::write(buf + offset, int32_t{100});
    offset += serializer::write(buf + offset, std::string_view{"abc"});
    offset += serializer::write(buf + offset, double{1.5});
    offset += serializer::write(buf + offset, bool{true});

    // 用 skip_param 遍历
    size_t pos = 0;
    size_t count = 0;
    while (pos < offset)
    {
        size_t step = serializer::skip_param(buf + pos);
        CHECK(step > 0, "skip_param returns > 0");
        pos += step;
        ++count;
    }
    CHECK(pos == offset, "skip_param consumed exactly all bytes");
    CHECK(count == 4, "skip_param iterated 4 params");
}

// ── Test 5: encoded_size 与实际写入字节数一致 ────────────────────────────
void test_encoded_size_consistency()
{
    std::cout << "\n[Test 5] encoded_size matches actual write\n";
    uint8_t buf[64];

    auto check = [&](auto val, const char* name)
    {
        size_t est = serializer::encoded_size(val);
        size_t act = serializer::write(buf, val);
        if (est != act)
        {
            std::cerr << "  FAIL: " << name << " encoded_size=" << est << " actual=" << act << "\n";
            g_res.failed++;
        }
        else
        {
            std::cout << "  PASS: " << name << " size match (" << act << "B)\n";
            g_res.passed++;
        }
    };

    check(int8_t{1}, "int8");
    check(int32_t{1}, "int32");
    check(int64_t{1}, "int64");
    check(float{1.f}, "float");
    check(double{1.}, "double");
    check(std::string_view{"test"}, "string_view");
}

// ── Test 6: write_header + make_entry_header ─────────────────────────────
void test_entry_header_integration()
{
    std::cout << "\n[Test 6] entry_header integration\n";
    auto hdr = make_entry_header(0xABCD1234, 3, log_level::warning);

    uint8_t buf[32];
    size_t n = serializer::write_header(buf, hdr);
    CHECK(n == 24, "write_header returns 24");

    entry_header out{};
    std::memcpy(&out, buf, sizeof(entry_header));
    CHECK(out.fmt_hash == 0xABCD1234u, "fmt_hash roundtrip");
    CHECK(out.category_idx == 3, "category_idx roundtrip");
    CHECK(out.level == uint8_t(log_level::warning), "level roundtrip");
    CHECK(out.timestamp_ms > 0, "timestamp > 0");
    CHECK(out.thread_id > 0, "thread_id > 0");
}

// ── Test 7: 性能基准 ──────────────────────────────────────────────────────
void test_serializer_performance()
{
    std::cout << "\n[Test 7] Serializer performance\n";
    constexpr int kIter = 2'000'000;
    alignas(64) uint8_t buf[64];
    volatile size_t sink = 0;

    // int32
    auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIter; ++i)
        sink += serializer::write(buf, int32_t{i});
    auto t1 = std::chrono::steady_clock::now();
    double ns_int32 =
        double(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) / kIter;
    std::cout << "  int32 write: " << ns_int32 << " ns/op\n";
    CHECK(ns_int32 < 10.0, "int32 write < 10ns");

    // string (16B)
    const std::string_view sv16 = "Hello, world!!!";
    t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kIter; ++i)
        sink += serializer::write(buf, sv16);
    t1 = std::chrono::steady_clock::now();
    double ns_str =
        double(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) / kIter;
    std::cout << "  string(16B) write: " << ns_str << " ns/op\n";
    CHECK(ns_str < 50.0, "string(16B) write < 50ns");

    (void)sink;
}

int main()
{
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║   QLog M4 Serializer Tests             ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";

    test_all_numeric_roundtrip();
    test_type_tag_values();
    test_string_roundtrip();
    test_skip_param();
    test_encoded_size_consistency();
    test_entry_header_integration();
    test_serializer_performance();

    std::cout << "\n══════════════════════════\n";
    std::cout << "PASSED: " << g_res.passed << "\n";
    std::cout << "FAILED: " << g_res.failed << "\n";
    return g_res.failed == 0 ? 0 : 1;
}