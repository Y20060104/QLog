#include "qlog/serialization/entry_format.h"
#include "qlog/serialization/serializer.h"

#include <cassert>
#include <cstring>
#include <iostream>

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

void test_header_layout()
{
    std::cout << "\n[Test] entry_header layout\n";
    CHECK(sizeof(entry_header) == 24, "size == 24");
    CHECK(alignof(entry_header) == 8, "align == 8");
    CHECK(offsetof(entry_header, timestamp_ms) == 0, "timestamp_ms offset");
    CHECK(offsetof(entry_header, thread_id) == 8, "thread_id offset");
    CHECK(offsetof(entry_header, fmt_hash) == 16, "fmt_hash offset");
    CHECK(offsetof(entry_header, category_idx) == 20, "category_idx offset");
    CHECK(offsetof(entry_header, level) == 22, "level offset");
}

void test_serialize_roundtrip_int32()
{
    std::cout << "\n[Test] int32 roundtrip\n";
    uint8_t buf[16];
    int32_t val = -12345;
    size_t n = serializer::write(buf, val);
    CHECK(n == 5, "int32 encoded size == 5");
    CHECK(buf[0] == static_cast<uint8_t>(param_type::type_int32), "type tag correct");
    CHECK(serializer::read_int32(buf) == val, "roundtrip value");
}

void test_serialize_roundtrip_float()
{
    std::cout << "\n[Test] float roundtrip\n";
    uint8_t buf[16];
    float val = 3.14159f;
    size_t n = serializer::write(buf, val);
    CHECK(n == 5, "float encoded size == 5");
    CHECK(serializer::read_float(buf) == val, "float roundtrip");
}

void test_serialize_roundtrip_string()
{
    std::cout << "\n[Test] string roundtrip\n";
    uint8_t buf[256];
    const std::string_view sv = "Hello, QLog!";
    size_t n = serializer::write(buf, sv);
    CHECK(n == 1 + 4 + sv.size(), "string encoded size");
    auto [rsv, consumed] = serializer::read_string(buf);
    CHECK(rsv == sv, "string content matches");
    CHECK(consumed == n, "consumed bytes match");
}

void test_serialize_string_truncation()
{
    std::cout << "\n[Test] string truncation at MAX_STRING_BYTES\n";
    // 超长字符串
    std::string big(MAX_STRING_BYTES + 100, 'A');
    uint8_t buf[MAX_STRING_BYTES + 64];
    size_t n = serializer::write(buf, std::string_view{big});
    CHECK(n == 1 + 4 + MAX_STRING_BYTES, "truncated to MAX_STRING_BYTES");
    auto [rsv, consumed] = serializer::read_string(buf);
    CHECK(rsv.size() == MAX_STRING_BYTES, "read back truncated length");
}

void test_header_write()
{
    std::cout << "\n[Test] header write\n";
    entry_header hdr{};
    hdr.timestamp_ms = 1234567890123ULL;
    hdr.thread_id = 42;
    hdr.fmt_hash = 0xDEADBEEF;
    hdr.category_idx = 7;
    hdr.level = static_cast<uint8_t>(log_level::info);

    uint8_t buf[32];
    size_t n = serializer::write_header(buf, hdr);
    CHECK(n == 24, "write_header returns 24");

    entry_header out{};
    std::memcpy(&out, buf, sizeof(entry_header));
    CHECK(out.timestamp_ms == hdr.timestamp_ms, "timestamp_ms roundtrip");
    CHECK(out.fmt_hash == hdr.fmt_hash, "fmt_hash roundtrip");
    CHECK(out.category_idx == hdr.category_idx, "category_idx roundtrip");
    CHECK(out.level == hdr.level, "level roundtrip");
}

int main()
{
    std::cout << "╔════════════════════════════════════════╗\n";
    std::cout << "║   QLog M4 Entry Format Tests           ║\n";
    std::cout << "╚════════════════════════════════════════╝\n";

    test_header_layout();
    test_serialize_roundtrip_int32();
    test_serialize_roundtrip_float();
    test_serialize_roundtrip_string();
    test_serialize_string_truncation();
    test_header_write();

    std::cout << "\n✅ All entry format tests passed!\n";
    return 0;
}