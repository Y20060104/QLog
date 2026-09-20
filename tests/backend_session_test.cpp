#include "manual_worker_fixture.hpp"
// As in diagnostics tests: inspect Impl without adding public testing APIs.
#include <gtest/gtest.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>

#include "../src/async_logger.cpp"

namespace {
int fault_fd = -1;
int write_fault = 0;
int fault_count = 0;
}  // namespace
extern "C" ssize_t __real_write(int, const void*, size_t);
extern "C" ssize_t __wrap_write(int fd, const void* data, size_t size) {
    if (fd == fault_fd && fault_count > 0) {
        --fault_count;
        errno = write_fault;
        return -1;
    }
    return __real_write(fd, data, size);
}
namespace qlog::detail {
struct AsyncLoggerTestAccess {
    static BackendSession& session(AsyncLogger& a) {
        return *a.impl_->session;
    }
    static ControlMailbox& mailbox(AsyncLogger& a) {
        return a.impl_->mailbox;
    }
    static ProducerContext* head(AsyncLogger& a) {
        return a.impl_->published_context.load();
    }
    static ManagementController& management(AsyncLogger& a) {
        return a.impl_->management;
    }
};
struct ManagementControllerTestAccess {
    static void exhaust_next(ManagementController& m) {
        m.next_id_ = UINT64_MAX;
    }
};
}  // namespace qlog::detail
namespace {
using namespace qlog;
using namespace qlog::detail;
using Access = AsyncLoggerTestAccess;
std::string read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
class BackendSessionTest : public ::testing::Test {
   protected:
    std::string directory;
    LoggerConfig config;
    test::ManualProvider* provider{};
    void SetUp() override {
        provider = &test::install_manual_worker(false);
        char pattern[] = "/tmp/qlog-session-XXXXXX";
        auto* p = ::mkdtemp(pattern);
        ASSERT_NE(p, nullptr);
        directory = p;
        config.name = "session";
        AppenderConfig a;
        a.name = "file";
        a.type = AppenderType::TextFile;
        a.file.path = directory + "/first.log";
        config.appenders = {a};
    }
    void TearDown() override {
        fault_fd = -1;
        fault_count = 0;
        std::filesystem::remove_all(directory);
    }
    int descriptor(const std::string& path) {
        for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
            std::error_code error;
            if (std::filesystem::read_symlink(entry.path(), error) == path && !error)
                return std::stoi(entry.path().filename().string());
        }
        return -1;
    }
    PollResult finish(AsyncLogger& logger, SubmitResult submitted) {
        EXPECT_EQ(submitted.status, SubmitStatus::submitted);
        auto& box = Access::mailbox(logger);
        EXPECT_TRUE(provider->latest->wait_for_command(box));
        return logger.poll_management(submitted.request_id);
    }
    void step(AsyncLogger& logger, unsigned n = 1) {
        BackendWorkspace workspace;
        while (n-- != 0)
            (void)Access::session(logger).service(workspace, std::chrono::steady_clock::now());
    }
};
TEST_F(BackendSessionTest, PublicDrainWritesFormattedRecordsAndStableShutdown) {
    AsyncLogger logger(config);
    ASSERT_TRUE(logger.try_log(0, LogLevel::info, "value={}", 42).accepted());
    auto result = finish(logger, logger.request_drain(FlushMode::durable));
    ASSERT_TRUE(result.completion);
    EXPECT_EQ(result.completion->status, CompletionStatus::completed);
    const auto text = read_all(config.appenders[0].file.path);
    EXPECT_NE(text.find("[INFO] [session/default] [tid="), std::string::npos);
    EXPECT_NE(text.find("value=42\n"), std::string::npos);
    const auto& first = logger.shutdown(FlushMode::durable);
    EXPECT_FALSE(first.backend_failed);
    EXPECT_FALSE(first.drain_incomplete);
    EXPECT_TRUE(first.errors.empty());
    EXPECT_EQ(&first, &logger.shutdown());
    EXPECT_EQ(logger.request_drain(FlushMode::buffered).status, SubmitStatus::stopped);
}
TEST_F(BackendSessionTest, FlushDoesNotDrainRing) {
    AsyncLogger logger(config);
    ASSERT_TRUE(logger.try_log(0, LogLevel::info, "queued").accepted());
    EXPECT_EQ(finish(logger, logger.request_flush_batches(FlushMode::buffered)).status,
              PollStatus::completed);
    EXPECT_TRUE(read_all(config.appenders[0].file.path).empty());
    (void)finish(logger, logger.request_drain(FlushMode::buffered));
    EXPECT_NE(read_all(config.appenders[0].file.path).find("queued"), std::string::npos);
}
TEST_F(BackendSessionTest, BusyBeforeOpenAndSingleReceipt) {
    AsyncLogger logger(config);
    const auto request = logger.request_flush_batches(FlushMode::buffered);
    auto replacement = config.appenders[0];
    replacement.file.path = directory + "/must-not-exist";
    EXPECT_EQ(logger.request_reset_appenders(&replacement, 1).status, SubmitStatus::busy);
    EXPECT_FALSE(std::filesystem::exists(replacement.file.path));
    EXPECT_EQ(logger.poll_management(request.request_id + 1).status, PollStatus::unknown_request);
    EXPECT_EQ(logger.poll_management(request.request_id).status, PollStatus::pending);
    provider->latest->wait_for_command(Access::mailbox(logger));
    EXPECT_EQ(logger.request_reset_appenders(&replacement, 1).status, SubmitStatus::busy);
    EXPECT_EQ(logger.poll_management(request.request_id).status, PollStatus::completed);
    EXPECT_EQ(logger.poll_management(request.request_id).status, PollStatus::unknown_request);
}
TEST_F(BackendSessionTest, CompatibleResetKeepsPendingBatchAndChangesTimezone) {
    AsyncLogger logger(config);
    ASSERT_TRUE(logger.try_log(0, LogLevel::info, "old").accepted());
    step(logger);
    auto replacement = config.appenders[0];
    replacement.text.time_zone.offset_minutes = 480;
    auto result = finish(logger, logger.request_reset_appenders(&replacement, 1));
    EXPECT_EQ(result.completion->status, CompletionStatus::applied);
    EXPECT_TRUE(read_all(replacement.file.path).empty());  // retained buffer, no forced flush
    ASSERT_TRUE(logger.try_log(0, LogLevel::info, "new").accepted());
    (void)finish(logger, logger.request_drain(FlushMode::buffered));
    const auto text = read_all(replacement.file.path);
    EXPECT_LT(text.find("old\n"), text.find("new\n"));
    EXPECT_NE(text.find("+00:00]"), std::string::npos);
    EXPECT_NE(text.find("+08:00]"), std::string::npos);
}
TEST_F(BackendSessionTest, IncompatibleResetRetiresOldAndPublishesFilter) {
    AsyncLogger logger(config);
    ASSERT_TRUE(logger.try_log(0, LogLevel::info, "old").accepted());
    step(logger);
    auto replacement = config.appenders[0];
    replacement.file.path = directory + "/new.log";
    replacement.filter.levels = 1U << static_cast<unsigned>(LogLevel::error);
    (void)finish(logger, logger.request_reset_appenders(&replacement, 1));
    EXPECT_NE(read_all(config.appenders[0].file.path).find("old\n"), std::string::npos);
    EXPECT_EQ(logger.try_log(0, LogLevel::info, "filtered").status(), LogStatus::filtered);
    ASSERT_TRUE(logger.try_log(0, LogLevel::error, "new").accepted());
    (void)logger.shutdown();
    EXPECT_NE(read_all(replacement.file.path).find("new\n"), std::string::npos);
}
TEST_F(BackendSessionTest, PreparationFailureDoesNotApplyAndDoesNotDeleteCreatedFile) {
    AsyncLogger logger(config);
    std::array<AppenderConfig, 2> replacement{config.appenders[0], config.appenders[0]};
    replacement[0].name = "first-new";
    replacement[0].file.path = directory + "/created.log";
    replacement[1].name = "second-new";
    replacement[1].file.path = directory;  // parent exists, but open rejects a directory
    EXPECT_EQ(logger.request_reset_appenders(replacement.data(), replacement.size()).status,
              SubmitStatus::open_failed);
    EXPECT_TRUE(std::filesystem::exists(replacement[0].file.path));
    EXPECT_EQ(Access::mailbox(logger).state.load(), CommandState::empty);
    ASSERT_TRUE(logger.try_log(0, LogLevel::info, "still-old").accepted());
    (void)logger.shutdown();
    EXPECT_NE(read_all(config.appenders[0].file.path).find("still-old"), std::string::npos);
}
TEST_F(BackendSessionTest, InvalidResetIsRejectedBeforeAnyOpen) {
    AsyncLogger logger(config);
    std::array<AppenderConfig, 2> replacement{config.appenders[0], config.appenders[0]};
    replacement[0].file.path = directory + "/must-not-exist";
    auto result = logger.request_reset_appenders(replacement.data(), 2);
    EXPECT_EQ(result.status, SubmitStatus::invalid_config);
    EXPECT_EQ(result.reason, ConfigError::duplicate_name);
    EXPECT_FALSE(std::filesystem::exists(replacement[0].file.path));
    EXPECT_EQ(logger.request_reset_appenders(nullptr, 1).status, SubmitStatus::invalid_config);
}
TEST_F(BackendSessionTest, SmallByteQuotaAlwaysProgressesAndRecordQuotaIsBounded) {
    config.backend.records_per_channel = 1;
    config.backend.bytes_per_channel = 1;
    AsyncLogger logger(config);
    for (int i = 0; i < 3; ++i)
        ASSERT_TRUE(logger.try_log(0, LogLevel::info, "n={}", i).accepted());
    step(logger);
#if QLOG_ENABLE_DIAGNOSTICS
    EXPECT_EQ(Access::session(logger).diagnostics().records, 1U);
#endif
    (void)finish(logger, logger.request_drain(FlushMode::buffered));
    const auto text = read_all(config.appenders[0].file.path);
    EXPECT_LT(text.find("n=0"), text.find("n=1"));
    EXPECT_LT(text.find("n=1"), text.find("n=2"));
}
TEST_F(BackendSessionTest, DiscoversMoreThanOneRegistrationChunk) {
    config.backend.records_per_channel = 1;
    AsyncLogger logger(config);
    for (unsigned i = 0; i < 70U; ++i) {
        std::thread t([&logger, i] {
            EXPECT_TRUE(logger.try_log(0, LogLevel::info, "thread={}", i).accepted());
        });
        t.join();
    }
    step(logger);
#if QLOG_ENABLE_DIAGNOSTICS
    EXPECT_EQ(Access::session(logger).diagnostics().records, 1U);
#endif
    (void)logger.shutdown();
    const auto text = read_all(config.appenders[0].file.path);
    EXPECT_EQ(std::count(text.begin(), text.end(), '\n'), 70);
}
TEST_F(BackendSessionTest, InvalidPayloadDoesNotFaultWholeChannel) {
    AsyncLogger logger(config);
    ASSERT_TRUE(logger.try_log(0, LogLevel::info, "bad").accepted());
    auto& ring = Access::head(logger)->channel_.ring_;
    auto frame = ring.try_read();
    ASSERT_TRUE(frame);
    const std::uint8_t invalid_flags = 3;
    std::memcpy(const_cast<std::byte*>(frame.data()) + offsetof(RecordHeader, flags),
                &invalid_flags, 1);
    ring.abandon(frame);
    ASSERT_TRUE(logger.try_log(0, LogLevel::info, "good").accepted());
    const auto& end = logger.shutdown();
    EXPECT_FALSE(end.drain_incomplete);
    const auto text = read_all(config.appenders[0].file.path);
    EXPECT_EQ(text.find("bad\n"), std::string::npos);
    EXPECT_NE(text.find("good\n"), std::string::npos);
}
TEST_F(BackendSessionTest, MultipleTargetsWithIndependentTimeZonesAndEmbeddedNul) {
    auto second = config.appenders[0];
    second.name = "second";
    second.file.path = directory + "/second.log";
    second.text.time_zone.offset_minutes = -60;
    config.appenders.push_back(second);
    AsyncLogger logger(config);
    const char message[] = {'a', '\0', 'b'};
    ASSERT_TRUE(logger.try_log(0, LogLevel::info, runtime_format(message, 3)).accepted());
    (void)logger.shutdown(FlushMode::durable);
    const auto one = read_all(config.appenders[0].file.path), two = read_all(second.file.path);
    EXPECT_NE(one.find("+00:00]"), std::string::npos);
    EXPECT_NE(two.find("-01:00]"), std::string::npos);
    EXPECT_NE(one.find(std::string("a\0b\n", 4)), std::string::npos);
    EXPECT_NE(two.find(std::string("a\0b\n", 4)), std::string::npos);
}
TEST_F(BackendSessionTest, ShutdownFinishesPendingResetBeforeDraining) {
    AsyncLogger logger(config);
    auto second = config.appenders[0];
    second.file.path = directory + "/second.log";
    const auto reset = logger.request_reset_appenders(&second, 1);
    ASSERT_EQ(reset.status, SubmitStatus::submitted);
    ASSERT_TRUE(logger.try_log(0, LogLevel::info, "after-reset").accepted());
    (void)logger.shutdown();
    EXPECT_NE(read_all(second.file.path).find("after-reset"), std::string::npos);
    EXPECT_EQ(logger.poll_management(reset.request_id).status, PollStatus::unknown_request);
}
TEST_F(BackendSessionTest, FailureCleanupHandlesPendingAndPartiallyRetiredReset) {
    AsyncLogger logger(config);
    ASSERT_TRUE(logger.try_log(0, LogLevel::info, "buffered").accepted());
    step(logger);
    auto replacement = config.appenders[0];
    replacement.file.path = directory + "/new.log";
    const auto reset = logger.request_reset_appenders(&replacement, 1);
    ASSERT_EQ(reset.status, SubmitStatus::submitted);
    // No worker in this fixture; explicitly certify no concurrent borrower.
    Access::session(logger).abandon_after_worker_failure();
    auto result = logger.poll_management(reset.request_id);
    ASSERT_TRUE(result.completion);
    EXPECT_EQ(result.completion->status, CompletionStatus::backend_failed);
    const auto& end = logger.shutdown();
    EXPECT_TRUE(end.backend_failed);
    EXPECT_TRUE(end.drain_incomplete);
    EXPECT_GT(end.retired_io.lost_bytes, 0U);
    EXPECT_TRUE(read_all(config.appenders[0].file.path).empty());
    EXPECT_TRUE(read_all(replacement.file.path).empty());
}
TEST_F(BackendSessionTest, RequestIdentityNeverWraps) {
    AsyncLogger logger(config);
    ManagementControllerTestAccess::exhaust_next(Access::management(logger));
    auto request = logger.request_flush_batches(FlushMode::buffered);
    EXPECT_EQ(request.request_id, UINT64_MAX);
    (void)finish(logger, request);
    EXPECT_EQ(logger.request_flush_batches(FlushMode::buffered).status,
              SubmitStatus::identity_exhausted);
}
TEST_F(BackendSessionTest, PreparationReordersReuseAndRecreateAllocatesAll) {
    auto old = prepare_appender_configs(config.appenders.data(), 1, 1);
    AppenderConfig another = old[0];
    another.name = "another";
    another.file.path = directory + "/another";
    old.push_back(another);
    const AppenderConfig reordered[]{old[1], old[0]};
    auto p =
        prepare_reset(reordered, 2, old.data(), 2, 1, ResetMode::reuse_compatible, provider->gate);
    EXPECT_EQ(p->reuse_old_index[0], 1U);
    EXPECT_EQ(p->reuse_old_index[1], 0U);
    EXPECT_FALSE(p->next_appenders[0]);
    EXPECT_FALSE(p->next_appenders[1]);
    p = prepare_reset(reordered, 2, old.data(), 2, 1, ResetMode::recreate_all, provider->gate);
    EXPECT_TRUE(p->next_appenders[0]);
    EXPECT_TRUE(p->next_appenders[1]);
}

TEST_F(BackendSessionTest, FailureRecoveryCleansAllResetOwnershipPhases) {
    for (unsigned visits : {0U, 1U, 3U, 4U}) {
        auto cfg = config;
        auto second = cfg.appenders[0];
        second.name = "second";
        second.file.path = directory + "/old-second";
        cfg.appenders.push_back(second);
        AsyncLogger logger(cfg);
        ASSERT_TRUE(logger.try_log(0, LogLevel::info, "buffered").accepted());
        step(logger);
        auto replacement = cfg.appenders[0];
        replacement.file.path = directory + "/replacement";
        const auto request = logger.request_reset_appenders(&replacement, 1);
        step(logger, visits);
        provider->latest->failed_flag = provider->latest->released_flag = true;
        const auto result = logger.poll_management(request.request_id);
        ASSERT_TRUE(result.completion);
        EXPECT_EQ(result.completion->status,
                  visits < 4U ? CompletionStatus::backend_failed : CompletionStatus::applied);
        EXPECT_EQ(logger.request_flush_batches(FlushMode::buffered).status, SubmitStatus::stopped);
        const auto& end = logger.shutdown();
        EXPECT_TRUE(end.backend_failed);
        EXPECT_TRUE(end.drain_incomplete);
    }
}
TEST_F(BackendSessionTest, PressureNotificationOnlyOnHalfFullOrFull) {
    config.ring = {256U, 128U};
    AsyncLogger logger(config);
    auto* attachment = provider->latest;
    EXPECT_EQ(attachment->notifications.load(), 0U);
    ASSERT_TRUE(logger.try_log(0, LogLevel::info, "a").accepted());
    EXPECT_EQ(attachment->notifications.load(), 0U);
    bool full = false;
    for (unsigned i = 0; i < 20U; ++i) {
        if (logger.try_log(0, LogLevel::info, "a").status() == LogStatus::full) {
            full = true;
            break;
        }
    }
    ASSERT_TRUE(full);
    const auto before = attachment->notifications.load();
    EXPECT_GT(before, 0U);
    EXPECT_EQ(logger.try_log(0, LogLevel::info, "a").status(), LogStatus::full);
    EXPECT_EQ(attachment->notifications.load(), before + 1U);
}
TEST_F(BackendSessionTest, DiskFullTargetDoesNotBlockHealthyTargetOrShutdown) {
    auto second = config.appenders[0];
    second.name = "healthy";
    second.file.path = directory + "/healthy";
    config.appenders.push_back(second);
    AsyncLogger logger(config);
    fault_fd = descriptor(config.appenders[0].file.path);
    ASSERT_GE(fault_fd, 0);
    write_fault = ENOSPC;
    fault_count = 1000;
    ASSERT_TRUE(logger.try_log(0, LogLevel::info, "deliver").accepted());
    auto completion = finish(logger, logger.request_drain(FlushMode::buffered));
    EXPECT_EQ(completion.completion->status, CompletionStatus::completed_with_io_error);
    EXPECT_NE(read_all(second.file.path).find("deliver"), std::string::npos);
    const auto& end = logger.shutdown();
    EXPECT_FALSE(end.errors.empty());
    EXPECT_GT(end.retired_io.lost_bytes, 0U);
    EXPECT_FALSE(end.backend_failed);
    EXPECT_FALSE(end.drain_incomplete);
}
TEST_F(BackendSessionTest, ShutdownDoesNotOpenRecoveryFile) {
    AsyncLogger logger(config);
    ASSERT_TRUE(logger.try_log(0, LogLevel::info, "lost").accepted());
    step(logger);
    fault_fd = descriptor(config.appenders[0].file.path);
    ASSERT_GE(fault_fd, 0);
    write_fault = EIO;
    fault_count = 1;
    auto completion = finish(logger, logger.request_flush_batches(FlushMode::buffered));
    EXPECT_EQ(completion.completion->status, CompletionStatus::completed_with_io_error);
    const auto& end = logger.shutdown();
    EXPECT_FALSE(end.errors.empty());
    EXPECT_GT(end.retired_io.lost_bytes, 0U);
    EXPECT_FALSE(std::filesystem::exists(config.appenders[0].file.path + ".recovery.0"));
}
TEST_F(BackendSessionTest, FrameReadPendingIsQuarantinedWithoutInventingAHandle) {
    AsyncLogger logger(config);
    ASSERT_TRUE(logger.try_log(0, LogLevel::info, "held").accepted());
    auto& ring = Access::head(logger)->channel_.ring_;
    auto frame = ring.try_read();
    ASSERT_TRUE(frame);
    // Deterministic injected protocol fault: Session must not release our handle.
    step(logger);
    EXPECT_TRUE(Access::head(logger)->consumer_faulted);
    ring.abandon(frame);  // test owns it, restore before queue destruction
    EXPECT_TRUE(logger.shutdown().drain_incomplete);
}

TEST_F(BackendSessionTest, EmptyNewChannelsCannotHideUnvisitedOldRecordDuringDrain) {
    AsyncLogger logger(config);
    for (unsigned i = 0; i < 70U; ++i) {
        std::thread t([&logger, i] {
            if (i == 0U)
                EXPECT_TRUE(logger.try_log(0, LogLevel::info, "oldest").accepted());
            else
                EXPECT_EQ(logger
                              .try_log(0, LogLevel::info,
                                       runtime_format(static_cast<const char*>(nullptr), 1))
                              .status(),
                          LogStatus::invalid_input);
        });
        t.join();
    }
    (void)logger.shutdown();
    EXPECT_NE(read_all(config.appenders[0].file.path).find("oldest"), std::string::npos);
}
}  // namespace
