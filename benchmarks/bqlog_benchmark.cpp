#include "adapters/bqlog_siso_adapter.hpp"
#include "benchmark_types.hpp"
#include "spsc_runner.hpp"

namespace qlog::bench {

BenchmarkResult run_bqlog_benchmark(Scenario scenario, const BenchmarkOptions& options,
                                    std::size_t repetition) {
    return run_adapter_benchmark<BqlogSisoAdapter>(Implementation::bqlog, scenario, options,
                                                   repetition);
}

}  // namespace qlog::bench
