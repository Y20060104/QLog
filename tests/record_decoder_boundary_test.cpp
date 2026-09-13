#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <qlog/detail/record_decoder.hpp>
#include <vector>
#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif
namespace {
using namespace qlog::detail;
const RecordValidationPolicy policy{{~0ULL, ~0ULL, ~0ULL, ~0ULL}, true};
std::vector<std::byte> golden() {
    std::vector<std::byte> b(32);
    const std::array<unsigned, 15> widths{1, 1, 1, 1, 2, 2, 4, 4, 8, 8, 4, 8, 8, 4, 0};
    for (unsigned tag = 1; tag <= 15; ++tag) {
        b.push_back(static_cast<std::byte>(tag));
        b.insert(b.end(), widths[tag - 1], std::byte{0});
    }
    const auto n = b.size() - 32U;
    for (unsigned i = 0; i < 4; ++i) b[20U + i] = static_cast<std::byte>((n >> (8U * i)) & 255U);
    b[28] = std::byte{15};
    return b;
}
TEST(RecordDecoderBoundary, EveryRecordPrefixAtEveryAlignment) {
    const auto full = golden();
    for (std::size_t offset = 0; offset < 32; ++offset)
        for (std::size_t n = 0; n <= full.size(); ++n)
            for (bool fix_outer : {false, true}) {
                SCOPED_TRACE(::testing::Message() << "offset=" << offset << " prefix=" << n
                                                  << " fix_outer=" << fix_outer);
                auto data = std::make_unique<std::byte[]>(n + offset);
                auto* start = data.get() + offset;
                std::copy_n(full.begin(), n, start);
                if (fix_outer && n >= 32)
                    for (unsigned i = 0; i < 4; ++i)
                        start[20U + i] = static_cast<std::byte>(((n - 32U) >> (8U * i)) & 255U);
                std::array<DecodedArg, 32> w{};
                const auto result = decode_v1(start, n, w.data(), 32, policy);
                EXPECT_EQ(result.succeeded(), n == full.size());
                if (!result.succeeded()) {
                    EXPECT_LE(result.failure()->error_offset, n);
                }
            }
}
#if defined(__linux__)
TEST(RecordDecoderBoundary, ProtectedPagesAtBothEndsForAllPrefixes) {
    const long page_result = sysconf(_SC_PAGESIZE);
    ASSERT_GT(page_result, 0);
    const auto page = static_cast<std::size_t>(page_result);
    void* raw = mmap(nullptr, 3U * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    ASSERT_NE(raw, MAP_FAILED);
    struct Mapping {
        void* address;
        std::size_t size;
        ~Mapping() {
            munmap(address, size);
        }
    } mapping{raw, 3U * page};
    auto* middle = static_cast<std::byte*>(raw) + page;
    ASSERT_EQ(mprotect(middle, page, PROT_READ | PROT_WRITE), 0);
    const auto full = golden();
    ASSERT_LT(full.size(), page);
    for (const bool at_end : {false, true})
        for (std::size_t n = 0; n <= full.size(); ++n) {
            auto* start = at_end ? middle + page - n : middle;
            if (n) std::copy_n(full.begin(), n, start);
            if (n >= 32)
                for (unsigned i = 0; i < 4; ++i)
                    start[20U + i] = static_cast<std::byte>(((n - 32U) >> (8U * i)) & 255U);
            std::array<DecodedArg, 32> w{};
            const auto result = decode_v1(start, n, w.data(), 32, policy);
            EXPECT_EQ(result.succeeded(), n == full.size());
        }
}
#endif
}  // namespace
