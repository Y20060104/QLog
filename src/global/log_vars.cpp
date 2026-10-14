#include "qlog/global/log_vars.hpp"

namespace qlog {
log_global_vars* log_global_var_default_initer_ = &log_global_vars::get();

log_global_vars::log_global_vars() = default;

void log_global_vars::partial_destruct() {
    // Manager/Appender members and their original shutdown flushing follow with those modules.
}
}  // namespace qlog
