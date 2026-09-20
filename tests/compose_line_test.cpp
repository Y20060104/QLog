#include <gtest/gtest.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <limits>
#include <string>
#include <thread>
#include <vector>

#include "qlog/detail/producer_context.hpp"
#include "qlog/detail/text_formatter.hpp"

namespace {
using namespace qlog;
using namespace qlog::detail;
const std::byte* bytes(std::string_view s) {
    return reinterpret_cast<const std::byte*>(s.data());
}
class ComposeLine : public ::testing::Test {
   protected:
    std::uint8_t enabled{1};
    FilterState filter{0x3f, &enabled, 1};
    ClockDescriptor clock{};
    RecordValidationPolicy policy{{0x3f, 0, 0, 0}, true};
    FormatHashDispatch hash{FormatHashDispatch::automatic()};
    std::string name{"logger"};
    std::vector<std::string> categories{"category"};
    ChannelDependencies deps{filter, clock, policy, hash, name, categories};
    ChannelCold cold{1, 1, 123, {}, 8192, deps};
    RecordHeader header{};
    CalendarCache cache;
    TimeZoneConfig zone;
    std::array<std::byte, 65538> output{};
    void SetUp() override {
        header.level = 2;
    }
    std::string render(std::string_view message = "body") {
        auto result = compose_line(cold, header, zone, cache, bytes(message), message.size(),
                                   output.data(), output.size());
        EXPECT_FALSE(result.failure);
        return {reinterpret_cast<const char*>(output.data()), result.size};
    }
};
TEST_F(ComposeLine, EpochAndExactFormat) {
    EXPECT_EQ(render(),
              "[1970-01-01T00:00:00.000000000+00:00] [INFO] [logger/category] [tid=123] body\n");
}
TEST_F(ComposeLine, CacheNanosecondsAndClockRollback) {
    header.time_value = 2999999999ULL;
    EXPECT_EQ(render().substr(0, 37), "[1970-01-01T00:00:02.999999999+00:00]");
    header.time_value = 2000000001ULL;
    EXPECT_NE(render().find("02.000000001"), std::string::npos);
    header.time_value = 1000000000ULL;
    EXPECT_NE(render().find("00:00:01.000000000"), std::string::npos);
    EXPECT_EQ(cache.local_second, 1);
}
TEST_F(ComposeLine, TimeZoneBoundsAndIndependentCaches) {
    zone.offset_minutes = -60;
    EXPECT_EQ(render().substr(0, 37), "[1969-12-31T23:00:00.000000000-01:00]");
    zone.offset_minutes = -840;
    EXPECT_EQ(render().substr(0, 37), "[1969-12-31T10:00:00.000000000-14:00]");
    CalendarCache other;
    zone.offset_minutes = 840;
    auto result = compose_line(cold, header, zone, other, nullptr, 0, output.data(), output.size());
    ASSERT_FALSE(result.failure);
    EXPECT_EQ(other.local_second, 50400);
    EXPECT_EQ(cache.local_second, -50400);
    EXPECT_EQ(render().substr(0, 37), "[1970-01-01T14:00:00.000000000+14:00]");
}
TEST_F(ComposeLine, LeapDayAndMaxTimestamp) {
    header.time_value = 951782400000000000ULL;  // 2000-02-29 UTC
    EXPECT_NE(render().find("2000-02-29T00:00:00"), std::string::npos);
    header.time_value = std::numeric_limits<std::uint64_t>::max();
    EXPECT_NE(render().find("2554-07-21T23:34:33.709551615"), std::string::npos);
}
TEST_F(ComposeLine, UnavailableFallbackAndAllLevels) {
    header.flags = 2;
    EXPECT_EQ(render(""), "[time=unavailable] [INFO] [logger/category] [tid=123] \n");
    header.flags = 1;
    EXPECT_NE(render().find("1970-01-01"), std::string::npos);
    const std::array<std::string_view, 6> levels{"TRACE",   "DEBUG", "INFO",
                                                 "WARNING", "ERROR", "FATAL"};
    for (std::uint8_t i = 0; i < 6; ++i) {
        header.level = i;
        EXPECT_NE(render().find("[" + std::string(levels[i]) + "]"), std::string::npos);
    }
}
TEST_F(ComposeLine, InvalidMetadataPointersAndTimeZone) {
    const auto check = [&](FormatError expected) {
        auto result =
            compose_line(cold, header, zone, cache, nullptr, 0, output.data(), output.size());
        ASSERT_TRUE(result.failure);
        EXPECT_EQ(result.failure->error, expected);
        EXPECT_EQ(result.size, 0U);
    };
    for (auto flags : {3U, 4U, 255U}) {
        header.flags = static_cast<std::uint8_t>(flags);
        check(FormatError::invalid_format_metadata);
    }
    header.flags = 2;
    header.time_value = 1;
    check(FormatError::invalid_format_metadata);
    header = {};
    header.level = 6;
    check(FormatError::invalid_format_metadata);
    header.level = 2;
    header.category_id = 1;
    check(FormatError::invalid_format_metadata);
    header.category_id = 0;
    zone.offset_minutes = 841;
    check(FormatError::invalid_arguments);
    zone.offset_minutes = -841;
    check(FormatError::invalid_arguments);
    zone.offset_minutes = 0;
    auto bad = compose_line(cold, header, zone, cache, nullptr, 1, output.data(), output.size());
    ASSERT_TRUE(bad.failure);
    EXPECT_EQ(bad.failure->error, FormatError::invalid_arguments);
    bad = compose_line(cold, header, zone, cache, nullptr, 0, nullptr, 1);
    ASSERT_TRUE(bad.failure);
    EXPECT_EQ(bad.failure->error, FormatError::invalid_workspace);
    bad = compose_line(cold, header, zone, cache, nullptr, 0, nullptr, 0);
    ASSERT_TRUE(bad.failure);
    EXPECT_EQ(bad.size, 0U);
}
TEST_F(ComposeLine, DisallowedFallbackIsRejected) {
    RecordValidationPolicy strict{{0x3f, 0, 0, 0}, false};
    ChannelDependencies strict_deps{filter, clock, strict, hash, name, categories};
    ChannelCold strict_cold{1, 1, 123, {}, 8192, strict_deps};
    header.flags = 1;
    auto result =
        compose_line(strict_cold, header, zone, cache, nullptr, 0, output.data(), output.size());
    ASSERT_TRUE(result.failure);
    EXPECT_EQ(result.failure->error, FormatError::invalid_format_metadata);
}
TEST_F(ComposeLine, ExactCapacityCanariesAnd64KiBLimit) {
    const auto expected = render();
    for (auto capacity : {expected.size(), expected.size() - 1}) {
        output.fill(std::byte{0xA5});
        auto result =
            compose_line(cold, header, zone, cache, bytes("body"), 4, output.data() + 1, capacity);
        EXPECT_EQ(output[0], std::byte{0xA5});
        EXPECT_EQ(output[capacity + 1], std::byte{0xA5});
        EXPECT_EQ(result.failure.has_value(), capacity != expected.size());
        EXPECT_EQ(result.size, capacity == expected.size() ? expected.size() : 0U);
    }
    const auto overhead = render("").size();
    std::string message(65536 - overhead, 'x');
    EXPECT_EQ(render(message).size(), 65536U);
    message.push_back('x');
    auto result = compose_line(cold, header, zone, cache, bytes(message), message.size(),
                               output.data(), output.size());
    ASSERT_TRUE(result.failure);
    EXPECT_EQ(result.size, 0U);
}
TEST_F(ComposeLine, PreservesNulAndNewlines) {
    name = std::string("lo\0g", 4);
    categories[0] = std::string("ca\0t", 4);
    const auto line = render(std::string("a\0b\nc", 5));
    EXPECT_NE(line.find(std::string("[lo\0g/ca\0t]", 11)), std::string::npos);
    EXPECT_EQ(line.substr(line.size() - 6), std::string("a\0b\nc\n", 6));
}
TEST_F(ComposeLine, RealContextCapturesCreatingThreadIdAndReusesContext) {
    std::atomic<ProducerContext*> head{nullptr};
    std::atomic<bool> open{true};
    ContextRegistryView view{allocate_logger_id(), {}, deps, head, open};
    std::uint64_t expected{}, actual{};
    bool reused = false;
    std::thread worker([&] {
        expected = static_cast<std::uint64_t>(::gettid());
        const auto first = acquire_thread_context(view);
        if (first.context()) {
            actual = first.context()->channel_.cold_.os_thread_id;
            reused = acquire_thread_context(view).context() == first.context();
        }
    });
    worker.join();
    EXPECT_NE(expected, 0U);
    EXPECT_EQ(actual, expected);
    EXPECT_TRUE(reused);
    auto* node = head.load();
    while (node) {
        auto* next = node->published_next;
        delete node;
        node = next;
    }
}
}  // namespace
