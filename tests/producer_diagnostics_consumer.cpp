#include "qlog/async_logger.hpp"
#ifndef QLOG_ENABLE_DIAGNOSTICS
#error QLOG_ENABLE_DIAGNOSTICS must be propagated by QLog::qlog
#endif
static_assert(QLOG_ENABLE_DIAGNOSTICS == QLOG_TEST_EXPECT_DIAGNOSTICS);
qlog::LogResult diagnostics_literal_consumer(qlog::AsyncLogger& logger) {
    return logger.try_log(0, qlog::LogLevel::info, "literal={}", 42);
}
qlog::LogResult diagnostics_view_consumer(qlog::AsyncLogger& logger) {
    return logger.try_log(0, qlog::LogLevel::info, qlog::runtime_format("view={}", 7), 42);
}
int diagnostics_consumer_mode() {
    return QLOG_ENABLE_DIAGNOSTICS;
}
