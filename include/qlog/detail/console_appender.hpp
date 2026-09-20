#pragma once
#include <mutex>

#include "qlog/detail/appender.hpp"

namespace qlog::detail {
// Runtime owns one gate shared by all shared/independent workers.
struct ConsoleOutputGate final {
    std::mutex mutex;
};
class ConsoleAppender final : public Appender {
   public:
    ConsoleAppender(AppenderConfig, ConsoleOutputGate&);

   private:
    ConsoleOutputGate& gate_;
    int fd_;
    IoWriteResult write(const std::byte*, std::size_t) noexcept override;
    IoCallResult sync_output() noexcept override;
    IoCallResult close_output() noexcept override;
    IoCallResult reopen_output(IoReport&, std::size_t) noexcept override;
    std::string_view output_path() const noexcept override;
};
}  // namespace qlog::detail
