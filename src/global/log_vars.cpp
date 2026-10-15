#include "qlog/global/log_vars.hpp"

namespace qlog {
log_global_vars* log_global_var_default_initer_ = &log_global_vars::get();

log_global_vars::log_global_vars() = default;

void log_global_vars::partial_destruct() {
    bool force_flush_success = false;
    for (std::uint32_t i = 0; i < 128; ++i) {
        if (log_manager_inst_.try_flush_all()) {
            force_flush_success = true;
            break;
        }
        qlog::platform::thread::sleep(1);
    }
    if (!force_flush_success) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::warning,
            "Force flush timed out on shutdown, some buffered logs may be lost");
    }
}
}  // namespace qlog
