#include "qlog/async_logger.hpp"
int main() {
    qlog::AsyncLogger logger(qlog::LoggerConfig{});
    const auto& result = logger.shutdown();
    return result.backend_failed || result.drain_incomplete ? 1 : 0;
}
