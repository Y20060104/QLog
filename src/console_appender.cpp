#include "qlog/detail/console_appender.hpp"

#include <unistd.h>

#include <cerrno>
#include <utility>

namespace qlog::detail {
ConsoleAppender::ConsoleAppender(AppenderConfig prepared, ConsoleOutputGate& gate)
    : Appender(std::move(prepared)),
      gate_(gate),
      fd_(config().console.stream == ConsoleStream::stdout_stream ? STDOUT_FILENO : STDERR_FILENO) {
}
IoWriteResult ConsoleAppender::write(const std::byte* data, std::size_t size) noexcept {
    // Worker must block SIGPIPE before output. Gate protects one syscall only.
    const std::lock_guard lock(gate_.mutex);
    const auto n = ::write(fd_, data, size);
    if (n < 0) return {0U, errno};
    return {static_cast<std::size_t>(n), 0};
}
IoCallResult ConsoleAppender::sync_output() noexcept {
    return {0};
}
IoCallResult ConsoleAppender::close_output() noexcept {
    return {0};
}
IoCallResult ConsoleAppender::reopen_output(IoReport&, std::size_t) noexcept {
    return {0};
}
std::string_view ConsoleAppender::output_path() const noexcept {
    return {};
}
}  // namespace qlog::detail
