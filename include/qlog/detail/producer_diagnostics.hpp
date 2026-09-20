#pragma once
#include <atomic>
#include <cstdint>

#if QLOG_ENABLE_DIAGNOSTICS
namespace qlog::detail {
struct alignas(64) ProducerCallCounters {
    std::atomic<std::uint64_t> calls{0};
    std::atomic<std::uint64_t> filtered{0};
    std::atomic<std::uint64_t> rejected_pre_admission{0};
};
struct alignas(64) ProducerAdmissionCounters {
    std::atomic<std::uint64_t> attempted{0};
    std::atomic<std::uint64_t> accepted{0};
    std::atomic<std::uint64_t> dropped_full{0};
    std::atomic<std::uint64_t> failed_after_attempt{0};
};
struct ProducerDiagnostics {
    ProducerCallCounters call;
    ProducerAdmissionCounters admission;
};
}  // namespace qlog::detail
#endif