#include "adapters/qlog_spsc_adapter.hpp"
#include "benchmark_types.hpp"
#include "spsc_runner.hpp"

namespace qlog::bench {

BenchmarkResult run_qlog_benchmark(Scenario scenario, const BenchmarkOptions& options,
                                   std::size_t repetition) {
    return run_adapter_benchmark<QlogSpscAdapter>(Implementation::qlog, scenario, options,
                                                  repetition);
}

}  // namespace qlog::bench
