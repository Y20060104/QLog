#include "qlog/detail/appender.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {
using namespace qlog;
using namespace qlog::detail;
const std::byte* bytes(const char* p) {
    return reinterpret_cast<const std::byte*>(p);
}
AppenderConfig config(AppenderType type = AppenderType::TextFile) {
    AppenderConfig c;
    c.name = "test";
    c.type = type;
    c.file.path = "/tmp/qlog-test-a-long-enough-path-for-preallocation.log";
    c.filter.category_enabled = {1U, 0U};
    c.text.batch_bytes = 65536U;
    return c;
}
struct Fake final : Appender {
    explicit Fake(const AppenderConfig& c) : Appender(c), path(c.file.path) {
        if (c.type == AppenderType::Console) path.clear();
        path.reserve(c.file.path.size() + 30U);
        output.reserve(1024U);
    }
    std::vector<IoWriteResult> steps;
    std::size_t cursor{};
    std::string path, output;
    int writes{}, syncs{}, closes{}, opens{};
    int sync_error{}, close_error{}, open_error{};
    bool fd_open{true};
    IoWriteResult write(const std::byte* data, std::size_t size) noexcept override {
        ++writes;
        const auto result = cursor < steps.size() ? steps[cursor++] : IoWriteResult{size, 0};
        if (result.written <= size && result.error == 0)
            output.append(reinterpret_cast<const char*>(data), result.written);
        return result;
    }
    IoCallResult sync_output() noexcept override {
        ++syncs;
        return {sync_error};
    }
    IoCallResult close_output() noexcept override {
        if (!fd_open) return {0};
        fd_open = false;
        ++closes;
        return {close_error};
    }
    IoCallResult reopen_output(IoReport& report, std::size_t index) noexcept override {
        ++opens;
        if (open_error != 0) {
            note_io_failure(report, index, IoStage::open, open_error, 0, false, path, true);
            return {open_error};
        }
        path = config().file.path;
        path += ".recovery.1";
        fd_open = true;
        return {0};
    }
    std::string_view output_path() const noexcept override {
        return path;
    }
};

TEST(OutputBatch, PrefixAndExactCapacity) {
    OutputBatch b(6);
    EXPECT_TRUE(b.append(bytes("abcdef"), 6));
    EXPECT_FALSE(b.append(bytes("x"), 1));
    b.consume(2);
    EXPECT_EQ(b.pending_size(), 4U);
    EXPECT_EQ(std::memcmp(b.pending_data(), "cdef", 4), 0);
    EXPECT_EQ(b.append_capacity(), 0U);
    b.consume(4);
    EXPECT_EQ(b.pending_size(), 0U);
    EXPECT_EQ(b.append_capacity(), 6U);
    ASSERT_TRUE(b.append(bytes("xyz"), 3));
    b.consume(1);
    EXPECT_EQ(b.discard_pending(), 2U);
    EXPECT_EQ(b.discard_pending(), 0U);
}
TEST(OutputBatch, InvalidAndEmpty) {
    EXPECT_THROW(OutputBatch(0), std::invalid_argument);
    OutputBatch b(1);
    EXPECT_TRUE(b.append(nullptr, 0));
    EXPECT_FALSE(b.append(nullptr, 1));
}
TEST(IoReport, AllTargetsAndAggregation) {
    std::array<AppenderConfig, 2> c{config(), config()};
    c[1].name = "second";
    auto r = prepare_io_report(c.data(), c.size());
    const std::string p = c[1].file.path + ".recovery.18446744073709551615";
    r.record(1, IoStage::write, ENOSPC, 2, false, p.data(), p.size());
    r.record(1, IoStage::write, EIO, 9, true, "new", 3);
    ASSERT_TRUE(r.has_errors());
    auto errors = r.take_errors();
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors[0].appender_name, "second");
    EXPECT_EQ(errors[0].system_error, ENOSPC);
    EXPECT_EQ(errors[0].unwritten_bytes, 9U);
    EXPECT_TRUE(errors[0].durability_uncertain);
    EXPECT_EQ(errors[0].output_path, p);
}
TEST(IoReport, EmptyClearAndMove) {
    static_assert(!std::is_copy_constructible_v<IoReport>);
    auto empty = prepare_io_report(nullptr, 0);
    EXPECT_FALSE(empty.has_errors());
    EXPECT_TRUE(empty.take_errors().empty());
    EXPECT_THROW(prepare_io_report(nullptr, 1), std::invalid_argument);
    auto c = config();
    auto r = prepare_io_report(&c, 1);
    r.record(0, IoStage::write, 0, 1, false, c.file.path.data(), c.file.path.size());
    r.clear();
    EXPECT_FALSE(r.has_errors());
    auto moved = std::move(r);
    moved.record(0, IoStage::open, EACCES, 0, false, c.file.path.data(), c.file.path.size());
    EXPECT_EQ(moved.take_errors()[0].system_error, EACCES);
}
TEST(Appender, FiltersAndLineBounds) {
    auto c = config();
    Fake a(c);
    EXPECT_TRUE(a.selects(0, 5));
    EXPECT_FALSE(a.selects(1, 0));
    EXPECT_FALSE(a.selects(2, 0));
    EXPECT_FALSE(a.selects(0, 255));
    EXPECT_FALSE(a.accept_line(nullptr, 1));
    EXPECT_FALSE(a.accept_line(bytes("x"), 65537));
    EXPECT_TRUE(a.ready_for_record());
    EXPECT_TRUE(a.accept_line(bytes("hello"), 5));
    EXPECT_FALSE(a.ready_for_record());
    EXPECT_EQ(a.writes, 0);
}
TEST(Appender, ShortEintrAndEmbeddedNul) {
    auto c = config();
    Fake a(c);
    auto r = prepare_io_report(&c, 1);
    a.steps = {{1, 0}, {0, EINTR}, {2, 0}};
    ASSERT_TRUE(a.accept_line(bytes("a\0bcde"), 6));
    EXPECT_EQ(a.flush(FlushMode::buffered, r, 0).status, FlushStatus::completed);
    EXPECT_EQ(a.output, std::string("a\0bcde", 6));
    EXPECT_EQ(a.writes, 4);
    EXPECT_FALSE(r.has_errors());
    EXPECT_TRUE(a.ready_for_record());
}
TEST(Appender, DiskFullKeepsOnlySuffixAndRespectsDeadline) {
    auto c = config();
    Fake a(c);
    auto r = prepare_io_report(&c, 1);
    a.steps = {{2, 0}, {0, ENOSPC}};
    ASSERT_TRUE(a.accept_line(bytes("abcdef"), 6));
    auto f = a.flush(FlushMode::buffered, r, 0);
    EXPECT_EQ(f.remaining_bytes, 4U);
    EXPECT_EQ(a.state(), OutputState::disk_full);
    EXPECT_FALSE(a.accept_line(bytes("x"), 1));
    const auto due = a.next_service_time();
    a.service(due - std::chrono::nanoseconds(1), r, 0);
    EXPECT_EQ(a.writes, 2);
    r.clear();
    a.service(due, r, 0);
    EXPECT_EQ(a.output, "abcdef");
    EXPECT_FALSE(r.has_errors());
    EXPECT_EQ(a.opens, 0);
    a.final_close(FlushMode::buffered, r, 0);
    RetiredIoSummary h;
    a.merge_history_into(h);
    EXPECT_EQ(h.event_count, 1U);
    EXPECT_EQ(h.lost_bytes, 0U);
}
TEST(Appender, ExplicitFlushDoesNotWaitForRetryDeadline) {
    auto c = config();
    Fake a(c);
    auto r = prepare_io_report(&c, 1);
    a.steps = {{0, EAGAIN}};
    ASSERT_TRUE(a.accept_line(bytes("abc"), 3));
    EXPECT_EQ(a.flush(FlushMode::buffered, r, 0).status, FlushStatus::failed);
    r.clear();
    EXPECT_EQ(a.flush(FlushMode::buffered, r, 0).status, FlushStatus::completed);
    EXPECT_EQ(a.output, "abc");
    EXPECT_FALSE(r.has_errors());
}
TEST(Appender, ZeroProgressIsNotAnOsEvent) {
    auto c = config();
    Fake a(c);
    auto r = prepare_io_report(&c, 1);
    a.steps = {{1, 0}, {0, 0}};
    ASSERT_TRUE(a.accept_line(bytes("abc"), 3));
    auto f = a.flush(FlushMode::buffered, r, 0);
    EXPECT_EQ(f.status, FlushStatus::incomplete);
    EXPECT_EQ(f.remaining_bytes, 2U);
    auto errors = r.take_errors();
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors[0].system_error, 0);
    EXPECT_EQ(errors[0].unwritten_bytes, 2U);
    r = prepare_io_report(&c, 1);
    a.final_close(FlushMode::buffered, r, 0);
    RetiredIoSummary h;
    a.merge_history_into(h);
    EXPECT_EQ(a.output, "abc");
    EXPECT_EQ(h.event_count, 0U);
    EXPECT_EQ(h.lost_bytes, 0U);
}
TEST(Appender, PermanentFailureAndRecoveryPreserveHistory) {
    auto c = config();
    Fake a(c);
    auto r = prepare_io_report(&c, 1);
    a.steps = {{2, 0}, {0, EIO}};
    ASSERT_TRUE(a.accept_line(bytes("abcdef"), 6));
    EXPECT_EQ(a.flush(FlushMode::buffered, r, 0).status, FlushStatus::failed);
    EXPECT_EQ(a.state(), OutputState::reopen_pending);
    EXPECT_EQ(a.closes, 1);
    r.clear();
    EXPECT_EQ(a.flush(FlushMode::buffered, r, 0).status, FlushStatus::failed);
    EXPECT_TRUE(r.has_errors());
    EXPECT_EQ(a.writes, 2);
    r.clear();
    a.service(a.next_service_time(), r, 0);
    EXPECT_EQ(a.opens, 1);
    EXPECT_EQ(a.writes, 2);
    EXPECT_TRUE(a.ready_for_record());
    EXPECT_TRUE(a.accept_line(bytes("next"), 4));
    a.final_close(FlushMode::buffered, r, 0);
    EXPECT_EQ(a.output, "abnext");
    EXPECT_FALSE(r.has_errors());
    RetiredIoSummary h;
    a.merge_history_into(h);
    a.merge_history_into(h);
    EXPECT_EQ(h.event_count, 1U);
    EXPECT_EQ(h.lost_bytes, 4U);
    ASSERT_TRUE(h.first_error);
    EXPECT_EQ(h.first_error->output_path, c.file.path);
}
TEST(Appender, RecoveryOpenFailureIsCountedOnce) {
    auto c = config();
    Fake a(c);
    auto r = prepare_io_report(&c, 1);
    a.steps = {{0, EIO}};
    ASSERT_TRUE(a.accept_line(bytes("x"), 1));
    (void)a.flush(FlushMode::buffered, r, 0);
    r.clear();
    a.open_error = EACCES;
    a.service(a.next_service_time(), r, 0);
    EXPECT_EQ(a.state(), OutputState::reopen_pending);
    EXPECT_TRUE(r.has_errors());
    a.final_close(FlushMode::buffered, r, 0);
    RetiredIoSummary h;
    a.merge_history_into(h);
    EXPECT_EQ(h.event_count, 2U);
    EXPECT_EQ(h.lost_bytes, 1U);
    EXPECT_EQ(a.closes, 1);
}
TEST(Appender, RecoveryPathIsUsedForNewFailures) {
    auto c = config();
    Fake a(c);
    auto r = prepare_io_report(&c, 1);
    a.steps = {{0, EIO}};
    ASSERT_TRUE(a.accept_line(bytes("x"), 1));
    (void)a.flush(FlushMode::buffered, r, 0);
    a.service(a.next_service_time(), r, 0);
    r.clear();
    a.steps.push_back({0, ENOSPC});
    ASSERT_TRUE(a.accept_line(bytes("y"), 1));
    (void)a.flush(FlushMode::buffered, r, 0);
    EXPECT_EQ(r.take_errors()[0].output_path, c.file.path + ".recovery.1");
}
TEST(Appender, SyncIsOneCallEvenOnEintrAndCanRecover) {
    auto c = config();
    Fake a(c);
    auto r = prepare_io_report(&c, 1);
    ASSERT_TRUE(a.accept_line(bytes("abc"), 3));
    a.sync_error = EINTR;
    auto f = a.flush(FlushMode::durable, r, 0);
    EXPECT_EQ(f.status, FlushStatus::failed);
    EXPECT_TRUE(f.durability_uncertain);
    EXPECT_EQ(a.syncs, 1);
    EXPECT_EQ(a.output, "abc");
    EXPECT_EQ(a.state(), OutputState::active);
    r.clear();
    (void)a.flush(FlushMode::buffered, r, 0);
    EXPECT_TRUE(r.has_errors());
    EXPECT_EQ(a.syncs, 1);
    r.clear();
    a.sync_error = 0;
    f = a.flush(FlushMode::durable, r, 0);
    EXPECT_EQ(f.status, FlushStatus::completed);
    EXPECT_FALSE(f.durability_uncertain);
    EXPECT_FALSE(r.has_errors());
    EXPECT_EQ(a.syncs, 2);
    EXPECT_EQ(a.writes, 1);
}
TEST(Appender, WriteAndSyncFailuresRemainSeparate) {
    auto c = config();
    Fake a(c);
    auto r = prepare_io_report(&c, 1);
    a.sync_error = EIO;
    (void)a.flush(FlushMode::durable, r, 0);
    r.clear();
    a.steps = {{0, ENOSPC}};
    ASSERT_TRUE(a.accept_line(bytes("x"), 1));
    (void)a.flush(FlushMode::buffered, r, 0);
    auto errors = r.take_errors();
    ASSERT_EQ(errors.size(), 2U);
    EXPECT_EQ(errors[0].stage, IoStage::write);
    EXPECT_EQ(errors[1].stage, IoStage::sync);
}
TEST(Appender, ConsoleDurableDoesNotSyncAndFailureDoesNotReopen) {
    auto c = config(AppenderType::Console);
    Fake a(c);
    auto r = prepare_io_report(&c, 1);
    a.steps = {{0, EPIPE}};
    ASSERT_TRUE(a.accept_line(bytes("abc"), 3));
    (void)a.flush(FlushMode::durable, r, 0);
    EXPECT_EQ(a.closes, 0);
    EXPECT_EQ(a.syncs, 0);
    r.clear();
    a.service(a.next_service_time(), r, 0);
    EXPECT_EQ(a.opens, 0);
    EXPECT_TRUE(a.ready_for_record());
    EXPECT_FALSE(r.has_errors());
}
TEST(Appender, FinalFailureDiscardsOnceWithoutRecovery) {
    auto c = config();
    Fake a(c);
    auto r = prepare_io_report(&c, 1);
    a.steps = {{2, 0}, {0, ENOSPC}};
    ASSERT_TRUE(a.accept_line(bytes("abcdef"), 6));
    a.close_error = EINTR;
    a.final_close(FlushMode::buffered, r, 0);
    a.final_close(FlushMode::buffered, r, 0);
    EXPECT_EQ(a.closes, 1);
    EXPECT_EQ(a.opens, 0);
    EXPECT_EQ(a.writes, 2);
    auto errors = r.take_errors();
    ASSERT_EQ(errors.size(), 2U);
    EXPECT_EQ(errors[0].stage, IoStage::write);
    EXPECT_EQ(errors[1].stage, IoStage::close);
    RetiredIoSummary h;
    a.merge_history_into(h);
    a.merge_history_into(h);
    EXPECT_EQ(h.event_count, 2U);
    EXPECT_EQ(h.lost_bytes, 4U);
}
TEST(Appender, FinalZeroProgressDiscardsWithoutOsEvent) {
    auto c = config();
    Fake a(c);
    auto r = prepare_io_report(&c, 1);
    a.steps = {{0, 0}};
    ASSERT_TRUE(a.accept_line(bytes("abc"), 3));
    a.final_close(FlushMode::buffered, r, 0);
    RetiredIoSummary h;
    a.merge_history_into(h);
    EXPECT_EQ(h.event_count, 0U);
    EXPECT_EQ(h.lost_bytes, 3U);
    EXPECT_FALSE(h.first_error);
    EXPECT_TRUE(r.has_errors());
}
TEST(Appender, CloseErrorDoesNotOverwriteBlockingWriteFault) {
    auto c = config();
    Fake a(c);
    auto r = prepare_io_report(&c, 1);
    a.steps = {{0, EIO}};
    a.close_error = EBADF;
    ASSERT_TRUE(a.accept_line(bytes("abc"), 3));
    (void)a.flush(FlushMode::buffered, r, 0);
    r.clear();
    (void)a.flush(FlushMode::buffered, r, 0);
    auto errors = r.take_errors();
    ASSERT_EQ(errors.size(), 1U);
    EXPECT_EQ(errors[0].stage, IoStage::write);
    EXPECT_EQ(errors[0].system_error, EIO);
}
TEST(Appender, ConfigSwapPreservesBatchAndUsesConfiguredPeriod) {
    auto c = config();
    Fake a(c);
    auto r = prepare_io_report(&c, 1);
    ASSERT_TRUE(a.accept_line(bytes("abc"), 3));
    a.calendar_cache().valid = true;
    auto replacement = c;
    replacement.enabled = false;
    replacement.text.time_zone.offset_minutes = 480;
    replacement.text.flush_interval_us = 700000;
    a.apply_compatible_config(replacement);
    EXPECT_FALSE(a.selects(0, 0));
    EXPECT_TRUE(replacement.enabled);
    EXPECT_FALSE(a.calendar_cache().valid);
    auto due = a.next_service_time();
    a.service(due - std::chrono::nanoseconds(1), r, 0);
    EXPECT_EQ(a.writes, 0);
    a.service(due, r, 0);
    EXPECT_EQ(a.output, "abc");
    due = a.next_service_time();
    a.service(due, r, 0);
    EXPECT_EQ(a.next_service_time() - due, std::chrono::microseconds(700000));
}
TEST(Appender, ClosedFlushIsReportedAndHistoryKeepsFirstAcrossTargets) {
    auto c = config();
    Fake a(c);
    auto r = prepare_io_report(&c, 1);
    a.final_close(FlushMode::buffered, r, 0);
    EXPECT_EQ(a.flush(FlushMode::buffered, r, 0).status, FlushStatus::failed);
    EXPECT_TRUE(r.has_errors());
    EXPECT_FALSE(a.ready_for_record());
    EXPECT_EQ(a.next_service_time(), std::chrono::steady_clock::time_point::max());
}

TEST(Appender, SessionCapturesFirstErrorBeforeRetirementOrderChanges) {
    auto c = config();
    Fake earlier(c), later(c);
    auto r = prepare_io_report(&c, 1);
    RetiredIoSummary history;
    earlier.steps = {{0, ENOSPC}};
    ASSERT_TRUE(earlier.accept_line(bytes("a"), 1));
    (void)earlier.flush(FlushMode::buffered, r, 0);
    earlier.capture_first_error(history);
    later.steps = {{0, EIO}};
    ASSERT_TRUE(later.accept_line(bytes("b"), 1));
    later.final_close(FlushMode::buffered, r, 0);
    later.capture_first_error(history);
    later.merge_history_into(history);
    earlier.final_close(FlushMode::buffered, r, 0);
    earlier.merge_history_into(history);
    ASSERT_TRUE(history.first_error);
    EXPECT_EQ(history.first_error->system_error, ENOSPC);
    EXPECT_EQ(history.event_count, 2U);
    EXPECT_EQ(history.lost_bytes, 1U);
}
}  // namespace
