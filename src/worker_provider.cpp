#include "qlog/detail/worker_provider.hpp"

#include <atomic>
#include <stdexcept>
namespace qlog::detail {
namespace {
std::atomic<WorkerProvider*> installed{nullptr};
}
void install_worker_provider(WorkerProvider& provider) {
    WorkerProvider* empty = nullptr;
    if (!installed.compare_exchange_strong(empty, &provider, std::memory_order_release,
                                           std::memory_order_relaxed) &&
        empty != &provider)
        throw std::logic_error("QLog worker provider already installed");
}
WorkerProvider& default_worker_provider();
WorkerProvider& worker_provider() {
    auto* provider = installed.load(std::memory_order_acquire);
    if (!provider) {
        auto* candidate = &default_worker_provider();
        if (installed.compare_exchange_strong(provider, candidate, std::memory_order_acq_rel))
            provider = candidate;
    }
    return *provider;
}
}  // namespace qlog::detail
