#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <qlog/detail/format_hash_reference.hpp>
#include <string_view>
#include <vector>

#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "format_hash_test_access.hpp"

namespace {
using namespace qlog::detail;
using format_hash_reference::hash_raw_ref;

static_assert(hash_literal_stored("") == 1U);
static_assert(hash_literal_stored("a") == 0x33bbc03300000000ULL);
static_assert(hash_literal_stored(u8"{}") == 0x000000000e3ee044ULL);
static_assert(hash_literal_stored("abc") == 0x33bbc033d64581afULL);
static_assert(hash_literal_stored(u8"123456789") == 0xb4ee537e6087809aULL);
static_assert(hash_literal_stored("a\0b") == hash_literal_stored(u8"a\0b"));
static_assert(hash_literal_stored("\x80\xff") == hash_literal_stored(u8"\x80\xff"));
constexpr auto kLiteral32 = [] {
    std::array<char, 33> text{};
    return hash_raw_ref(text.data(), 32U);
}();
static_assert(kLiteral32 == 0x187cd3cfe93daadbULL);

enum class Mode { software, automatic, hardware };
class FormatHash : public testing::TestWithParam<Mode> {
   protected:
    std::optional<FormatHashDispatch> dispatch;
    void SetUp() override {
        switch (GetParam()) {
            case Mode::software:
                dispatch = FormatHashTestAccess::software();
                break;
            case Mode::automatic:
                dispatch = FormatHashDispatch::automatic();
                break;
            case Mode::hardware:
                dispatch = FormatHashTestAccess::hardware();
                break;
        }
        if (!dispatch) GTEST_SKIP() << "x86 backend not compiled or CPU lacks SSE4.2";
    }
};

TEST_P(FormatHash, FrozenVectors) {
    struct Vector {
        std::vector<std::byte> bytes;
        std::uint64_t raw;
    };
    const auto text = [](std::string_view value) {
        const auto* p = reinterpret_cast<const std::byte*>(value.data());
        return std::vector<std::byte>(p, p + value.size());
    };
    std::vector<Vector> vectors{
        {{}, 0U},
        {text("a"), 0x33bbc03300000000ULL},
        {text("{}"), 0x000000000e3ee044ULL},
        {text("abc"), 0x33bbc033d64581afULL},
        {text("123456789"), 0xb4ee537e6087809aULL},
        {std::vector<std::byte>(32U), 0x187cd3cfe93daadbULL},
        {std::vector<std::byte>(33U), 0xe166cd00cec7b109ULL},
        {std::vector<std::byte>(64U), 0x977cb6d90d3cc2e4ULL},
    };
    for (std::size_t v = 6U; v < vectors.size(); ++v)
        for (std::size_t i = 0U; i < vectors[v].bytes.size(); ++i)
            vectors[v].bytes[i] = static_cast<std::byte>(i);
    for (const auto& v : vectors) {
        SCOPED_TRACE(v.bytes.size());
        EXPECT_EQ(hash_raw_ref(v.bytes.data(), v.bytes.size()), v.raw);
        EXPECT_EQ(dispatch->hash_raw_unchecked(v.bytes.data(), v.bytes.size()), v.raw);
        std::vector<std::byte> out(v.bytes.size());
        EXPECT_EQ(dispatch->copy_raw_unchecked(v.bytes.data(), out.data(), v.bytes.size()), v.raw);
        EXPECT_EQ(out, v.bytes);
        const auto result = hash_format_stored(*dispatch, v.bytes.data(), v.bytes.size());
        ASSERT_TRUE(result.succeeded());
        EXPECT_EQ(*result.stored_hash(), v.raw == 0U ? 1U : v.raw);
        EXPECT_EQ(result.failure(), nullptr);
    }
}

TEST_P(FormatHash, LengthAlignmentAndExactCopyMatrix) {
    std::vector<std::size_t> sizes;
    for (std::size_t i = 0U; i <= 128U; ++i) sizes.push_back(i);
    for (auto i : {255U, 256U, 257U, 8191U, 8192U, 8193U}) sizes.push_back(i);
    constexpr auto canary = std::byte{0xA5};
    for (const auto size : sizes) {
        SCOPED_TRACE(size);
        std::vector<std::byte> value(size);
        for (std::size_t i = 0; i < size; ++i)
            value[i] = static_cast<std::byte>((i * 131U + size * 17U) & 255U);
        const auto expected = hash_raw_ref(value.data(), size);
        for (std::size_t so = 0; so < 32U; ++so) {
            std::vector<std::byte> source(size + 96U, canary);
            auto* src = source.data() + 32U + so;
            std::copy(value.begin(), value.end(), src);
            const auto before = source;
            ASSERT_EQ(dispatch->hash_raw_unchecked(src, size), expected);
            for (std::size_t offset = 0; offset < 32U; ++offset) {
                std::vector<std::byte> output(size + 96U, canary);
                auto* dst = output.data() + 32U + offset;
                const auto result =
                    copy_and_hash_format_stored(*dispatch, src, size, dst, size + 1U);
                ASSERT_TRUE(result.succeeded());
                ASSERT_EQ(*result.stored_hash(), expected == 0U ? 1U : expected);
                ASSERT_TRUE(std::equal(value.begin(), value.end(), dst));
                ASSERT_TRUE(std::all_of(output.data(), dst, [](auto b) { return b == canary; }));
                ASSERT_TRUE(std::all_of(dst + size, output.data() + output.size(),
                                        [](auto b) { return b == canary; }));
            }
            ASSERT_EQ(source, before);
        }
    }
}

TEST_P(FormatHash, RandomInputsWithExactHeapAllocations) {
    std::uint32_t state = 0x12345678U;
    const auto next = [&state] {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        return state;
    };
    for (unsigned trial = 0; trial < 128U; ++trial) {
        const std::size_t size = next() % 16385U;
        auto src = std::make_unique<std::byte[]>(size);
        auto dst = std::make_unique<std::byte[]>(size);
        for (std::size_t i = 0; i < size; ++i) src[i] = static_cast<std::byte>(next() & 255U);
        const auto expected = hash_raw_ref(src.get(), size);
        EXPECT_EQ(dispatch->hash_raw_unchecked(src.get(), size), expected);
        EXPECT_EQ(dispatch->copy_raw_unchecked(src.get(), dst.get(), size), expected);
        EXPECT_TRUE(std::equal(src.get(), src.get() + size, dst.get()));
    }
}

TEST_P(FormatHash, EmptyNullAndCheckedFailurePriority) {
    EXPECT_EQ(dispatch->hash_raw_unchecked(nullptr, 0U), 0U);
    EXPECT_EQ(dispatch->copy_raw_unchecked(nullptr, nullptr, 0U), 0U);
    const auto empty = copy_and_hash_format_stored(*dispatch, nullptr, 0U, nullptr, 0U);
    ASSERT_TRUE(empty.succeeded());
    EXPECT_EQ(*empty.stored_hash(), 1U);
    std::array<std::byte, 16> source{}, destination{};
    destination.fill(std::byte{0xCC});
    const auto before = destination;
    const auto check = [](const HashResult& r, HashError error) {
        EXPECT_FALSE(r.succeeded());
        EXPECT_EQ(r.stored_hash(), nullptr);
        ASSERT_NE(r.failure(), nullptr);
        EXPECT_EQ(*r.failure(), error);
    };
    check(hash_format_stored(*dispatch, nullptr, 1U), HashError::invalid_source_metadata);
    check(copy_and_hash_format_stored(*dispatch, nullptr, 1U, nullptr, 1U),
          HashError::invalid_source_metadata);
    check(copy_and_hash_format_stored(*dispatch, source.data(), 2U, nullptr, 1U),
          HashError::invalid_destination_metadata);
    check(copy_and_hash_format_stored(*dispatch, nullptr, 0U, nullptr, 1U),
          HashError::invalid_destination_metadata);
    check(copy_and_hash_format_stored(*dispatch, source.data(), 1U, nullptr, 0U),
          HashError::destination_too_small);
    check(copy_and_hash_format_stored(*dispatch, source.data(), 16U, destination.data(), 15U),
          HashError::destination_too_small);
    EXPECT_EQ(destination, before);
}

#if defined(__linux__)
class GuardPages {
   public:
    GuardPages()
        : page(static_cast<std::size_t>(::sysconf(_SC_PAGESIZE))),
          mapping(::mmap(nullptr, page * 3U, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)) {
        if (mapping != MAP_FAILED)
            ready = ::mprotect(static_cast<std::byte*>(mapping) + page, page,
                               PROT_READ | PROT_WRITE) == 0;
    }
    ~GuardPages() {
        if (mapping != MAP_FAILED) ::munmap(mapping, page * 3U);
    }
    GuardPages(const GuardPages&) = delete;
    GuardPages& operator=(const GuardPages&) = delete;
    std::byte* begin() {
        return static_cast<std::byte*>(mapping) + page;
    }
    const std::size_t page;
    void* const mapping;
    bool ready = false;
};
TEST_P(FormatHash, ProtectedPagesRejectReadsOrWritesBeyondEitherBoundary) {
    GuardPages source, destination;
    ASSERT_TRUE(source.ready);
    ASSERT_TRUE(destination.ready);
    for (std::size_t size = 1; size <= 257U; ++size) {
        for (bool at_end : {false, true}) {
            auto* src = source.begin() + (at_end ? source.page - size : 0U);
            auto* dst = destination.begin() + (at_end ? destination.page - size : 0U);
            for (std::size_t i = 0; i < size; ++i) src[i] = static_cast<std::byte>(i & 255U);
            const auto expected = hash_raw_ref(src, size);
            EXPECT_EQ(dispatch->hash_raw_unchecked(src, size), expected);
            EXPECT_EQ(dispatch->copy_raw_unchecked(src, dst, size), expected);
            EXPECT_TRUE(std::equal(src, src + size, dst));
        }
    }
}
#endif

TEST(FormatHashDispatch, AutomaticMatchesCompiledAndRuntimeCapability) {
    const auto automatic = FormatHashDispatch::automatic();
    EXPECT_EQ(automatic.backend(),
              FormatHashTestAccess::hardware() ? HashBackend::x86_crc32c : HashBackend::software);
    EXPECT_EQ(FormatHashTestAccess::software().backend(), HashBackend::software);
#if !QLOG_TEST_HAS_X86_CRC32C
    EXPECT_FALSE(hash_impl::x86_crc32c_available());
#endif
}

INSTANTIATE_TEST_SUITE_P(Backends, FormatHash,
                         testing::Values(Mode::software, Mode::automatic, Mode::hardware),
                         [](const auto& p) {
                             switch (p.param) {
                                 case Mode::software:
                                     return "Software";
                                 case Mode::automatic:
                                     return "Automatic";
                                 case Mode::hardware:
                                     return "Hardware";
                             }
                             return "Unknown";
                         });
}  // namespace
