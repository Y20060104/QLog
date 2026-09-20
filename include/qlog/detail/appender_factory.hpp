#pragma once
#include <memory>

#include "qlog/detail/console_appender.hpp"
#include "qlog/detail/text_file_appender.hpp"

namespace qlog::detail {
// Cold path only; input must come from prepare_appender_configs.
std::unique_ptr<Appender> make_appender(AppenderConfig prepared, ConsoleOutputGate& gate);
}  // namespace qlog::detail
