#pragma once
#include <memory>

#include "qlog/detail/backend_session.hpp"
namespace qlog::detail {
// Implemented by the built-in worker/runtime. All false wait results certify that
// the worker has exited/released every Session borrow. Never detach a live thread.
class WorkerAttachment {
   public:
    virtual ~WorkerAttachment() noexcept = default;
    virtual void notify() noexcept = 0;
    virtual bool failed() const noexcept = 0;
    virtual bool failed_and_released() const noexcept = 0;
    // May block; true means mailbox completed. No management ownership changes.
    virtual bool wait_for_command(ControlMailbox&) noexcept = 0;
    // Called after request_stop. true means acknowledge_detached completed;
    // independent worker MUST also be joined; shared worker stays alive.
    // Idempotent, and must work after an earlier failure cleanup.
    virtual bool finish_stop() noexcept = 0;
};
class WorkerProvider {
   public:
    virtual ~WorkerProvider() noexcept = default;
    virtual ConsoleOutputGate& console_gate() noexcept = 0;
    // Return only after ready+attached. Throw only after all borrows are released.
    virtual std::unique_ptr<WorkerAttachment> attach(BackendSession&, ThreadMode) = 0;
};
// Internal test/embedding override; applications use the built-in runtime. Install before
// constructing loggers; provider/gate must outlive all attachments. Never replace.
void install_worker_provider(WorkerProvider&);
WorkerProvider& worker_provider();  // lazily installs the production runtime
}  // namespace qlog::detail
