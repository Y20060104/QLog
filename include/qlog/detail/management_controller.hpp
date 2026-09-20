#pragma once
#include "qlog/detail/control_mailbox.hpp"
namespace qlog::detail {
// Single management caller. Never reads worker-owned Appender configuration.
class ManagementController final {
   public:
    ManagementController(ControlMailbox&, std::vector<AppenderConfig>, std::size_t,
                         ConsoleOutputGate&);
    SubmitResult reset(const AppenderConfig*, std::size_t, ResetMode);
    SubmitResult flush(FlushMode);
    SubmitResult drain(FlushMode);
    PollResult poll(std::uint64_t);
    void stop_submissions() noexcept {
        stopped_ = true;
    }

   private:
    friend struct ManagementControllerTestAccess;
    ControlMailbox& mailbox_;
    std::vector<AppenderConfig> acknowledged_;
    std::size_t categories_;
    ConsoleOutputGate& gate_;
    std::uint64_t next_id_{1U};
    bool stopped_{false};
    SubmitStatus admission() const noexcept;
    SubmitResult submit(CommandKind, FlushMode, const AppenderConfig*, std::size_t, ResetMode);
};
}  // namespace qlog::detail
