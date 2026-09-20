#include "qlog/detail/appender_factory.hpp"

#include <stdexcept>
#include <utility>

namespace qlog::detail {
std::unique_ptr<Appender> make_appender(AppenderConfig prepared, ConsoleOutputGate& gate) {
    switch (prepared.type) {
        case AppenderType::Console:
            return std::make_unique<ConsoleAppender>(std::move(prepared), gate);
        case AppenderType::TextFile:
            return std::make_unique<TextFileAppender>(std::move(prepared));
    }
    throw std::invalid_argument("invalid Appender type");
}
}  // namespace qlog::detail
