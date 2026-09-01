#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "benchmark_types.hpp"
#include "linux_support.hpp"

#ifndef QLOG_BENCH_QLOG_COMMIT
#define QLOG_BENCH_QLOG_COMMIT "unknown"
#endif

#ifndef QLOG_BENCH_QLOG_DIRTY
#define QLOG_BENCH_QLOG_DIRTY 1
#endif

#if !QLOG_BENCH_WITH_BQLOG
#define QLOG_BENCH_BQLOG_COMMIT "disabled"
#define QLOG_BENCH_BQLOG_DIRTY 0
#endif

namespace qlog::bench {

const char* to_string(Implementation implementation) noexcept {
    switch (implementation) {
        case Implementation::qlog:
            return "qlog";
        case Implementation::bqlog:
            return "bqlog";
    }
    return "unknown";
}

const char* to_string(Scenario scenario) noexcept {
    switch (scenario) {
        case Scenario::transfer_retry:
            return "transfer_retry";
        case Scenario::drop_new:
            return "drop_new";
        case Scenario::full_fast_fail:
            return "full_fast_fail";
        case Scenario::write_success:
            return "write_success";
        case Scenario::prefilled_read:
            return "prefilled_read";
    }
    return "unknown";
}

namespace {

inline constexpr std::uint64_t kMaxBenchmarkDurationMs = 24U * 60U * 60U * 1000U;

struct CliOptions {
    std::string preset{"quick"};
    std::string implementation{"all"};
    std::string scenario{"all"};
    BenchmarkOptions benchmark{};
    OutputFormat output_format{OutputFormat::table};
    std::string output_path{};
    bool payload_overridden{};
    bool capacity_overridden{};
    bool warmup_overridden{};
    bool duration_overridden{};
    bool repeat_overridden{};
    bool help{};
};

[[nodiscard]] std::uint64_t parse_unsigned(std::string_view text, std::string_view option_name) {
    std::uint64_t value{};
    const char* const begin = text.data();
    const char* const end = text.data() + text.size();
    const auto [position, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || position != end) {
        throw std::invalid_argument("invalid value for " + std::string(option_name));
    }
    return value;
}

[[nodiscard]] int parse_cpu(std::string_view text, std::string_view option_name) {
    int value{};
    const char* const begin = text.data();
    const char* const end = text.data() + text.size();
    const auto [position, error] = std::from_chars(begin, end, value);
    if (error != std::errc{} || position != end || value < 0) {
        throw std::invalid_argument("invalid CPU for " + std::string(option_name));
    }
    return value;
}

[[nodiscard]] std::string_view require_value(int& index, int argc, char** argv,
                                             std::string_view option_name) {
    if (index + 1 >= argc) {
        throw std::invalid_argument("missing value for " + std::string(option_name));
    }
    ++index;
    return argv[index];
}

void validate_choice(std::string_view value, std::string_view option_name,
                     const std::vector<std::string_view>& choices) {
    if (std::find(choices.begin(), choices.end(), value) == choices.end()) {
        throw std::invalid_argument("invalid value for " + std::string(option_name));
    }
}

[[nodiscard]] CliOptions parse_cli(int argc, char** argv) {
    CliOptions options;
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument{argv[index]};
        if (argument == "--help" || argument == "-h") {
            options.help = true;
        } else if (argument == "--preset") {
            options.preset = require_value(index, argc, argv, argument);
            validate_choice(options.preset, argument, {"quick", "ring-v1"});
        } else if (argument == "--impl") {
            options.implementation = require_value(index, argc, argv, argument);
            validate_choice(options.implementation, argument, {"qlog", "bqlog", "all"});
        } else if (argument == "--scenario") {
            options.scenario = require_value(index, argc, argv, argument);
            validate_choice(options.scenario, argument,
                            {"transfer-retry", "drop-new", "full-fast-fail", "write-success",
                             "prefilled-read", "all"});
        } else if (argument == "--payload") {
            const std::uint64_t value =
                parse_unsigned(require_value(index, argc, argv, argument), argument);
            if (value > std::numeric_limits<std::size_t>::max()) {
                throw std::invalid_argument("payload exceeds size_t");
            }
            options.benchmark.payload_bytes = static_cast<std::size_t>(value);
            options.payload_overridden = true;
        } else if (argument == "--capacity") {
            const std::uint64_t value =
                parse_unsigned(require_value(index, argc, argv, argument), argument);
            if (value > std::numeric_limits<std::size_t>::max()) {
                throw std::invalid_argument("capacity exceeds size_t");
            }
            options.benchmark.capacity_bytes = static_cast<std::size_t>(value);
            options.capacity_overridden = true;
        } else if (argument == "--warmup-ms") {
            const std::uint64_t value =
                parse_unsigned(require_value(index, argc, argv, argument), argument);
            if (value > kMaxBenchmarkDurationMs) {
                throw std::invalid_argument("warmup duration must not exceed 24 hours");
            }
            options.benchmark.warmup = std::chrono::milliseconds(static_cast<std::int64_t>(value));
            options.warmup_overridden = true;
        } else if (argument == "--duration-ms") {
            const std::uint64_t value =
                parse_unsigned(require_value(index, argc, argv, argument), argument);
            if (value == 0U || value > kMaxBenchmarkDurationMs) {
                throw std::invalid_argument(
                    "duration must be positive and must not exceed 24 hours");
            }
            options.benchmark.duration =
                std::chrono::milliseconds(static_cast<std::int64_t>(value));
            options.duration_overridden = true;
        } else if (argument == "--repeat") {
            const std::uint64_t value =
                parse_unsigned(require_value(index, argc, argv, argument), argument);
            if (value == 0U || value > std::numeric_limits<std::size_t>::max()) {
                throw std::invalid_argument("repeat must be positive and representable");
            }
            options.benchmark.repeat = static_cast<std::size_t>(value);
            options.repeat_overridden = true;
        } else if (argument == "--producer-cpu") {
            options.benchmark.producer_cpu =
                parse_cpu(require_value(index, argc, argv, argument), argument);
        } else if (argument == "--consumer-cpu") {
            options.benchmark.consumer_cpu =
                parse_cpu(require_value(index, argc, argv, argument), argument);
        } else if (argument == "--format") {
            const std::string_view value = require_value(index, argc, argv, argument);
            validate_choice(value, argument, {"table", "jsonl"});
            options.output_format = value == "table" ? OutputFormat::table : OutputFormat::jsonl;
        } else if (argument == "--output") {
            options.output_path = require_value(index, argc, argv, argument);
        } else {
            throw std::invalid_argument("unknown option: " + std::string(argument));
        }
    }

    if (options.preset == "ring-v1") {
        if (!options.warmup_overridden) {
            options.benchmark.warmup = std::chrono::milliseconds(2000);
        }
        if (!options.duration_overridden) {
            options.benchmark.duration = std::chrono::milliseconds(5000);
        }
        if (!options.repeat_overridden) {
            options.benchmark.repeat = 7U;
        }
    }
    return options;
}

void print_help(std::ostream& output) {
    output << "QLog SPSC Ring benchmark\n\n"
           << "Usage: qlog_spsc_benchmark [options]\n\n"
           << "  --preset quick|ring-v1\n"
           << "  --impl qlog|bqlog|all\n"
           << "  --scenario transfer-retry|drop-new|full-fast-fail|write-success|"
              "prefilled-read|all\n"
           << "  --payload BYTES\n"
           << "  --capacity BYTES\n"
           << "  --warmup-ms N\n"
           << "  --duration-ms N\n"
           << "  --repeat N\n"
           << "  --producer-cpu N\n"
           << "  --consumer-cpu N\n"
           << "  --format table|jsonl\n"
           << "  --output PATH\n";
}

[[nodiscard]] Scenario parse_scenario(std::string_view value) {
    if (value == "transfer-retry") {
        return Scenario::transfer_retry;
    }
    if (value == "drop-new") {
        return Scenario::drop_new;
    }
    if (value == "full-fast-fail") {
        return Scenario::full_fast_fail;
    }
    if (value == "write-success") {
        return Scenario::write_success;
    }
    if (value == "prefilled-read") {
        return Scenario::prefilled_read;
    }
    throw std::invalid_argument("scenario must name one concrete scenario");
}

[[nodiscard]] std::vector<Scenario> selected_scenarios(const CliOptions& options) {
    if (options.scenario != "all") {
        return {parse_scenario(options.scenario)};
    }
    return {
        Scenario::transfer_retry, Scenario::drop_new,       Scenario::full_fast_fail,
        Scenario::write_success,  Scenario::prefilled_read,
    };
}

[[nodiscard]] const char* primary_rate_metric(Scenario scenario) noexcept {
    switch (scenario) {
        case Scenario::transfer_retry:
        case Scenario::drop_new:
        case Scenario::write_success:
            return "accepted_records";
        case Scenario::full_fast_fail:
            return "full_calls";
        case Scenario::prefilled_read:
            return "read_records";
    }
    return "unknown";
}

void add_case(std::vector<BenchmarkCase>& cases, BenchmarkCase candidate) {
    const auto duplicate =
        std::find_if(cases.begin(), cases.end(), [&candidate](const BenchmarkCase& existing) {
            return existing.scenario == candidate.scenario &&
                   existing.capacity_bytes == candidate.capacity_bytes &&
                   existing.payload_bytes == candidate.payload_bytes;
        });
    if (duplicate == cases.end()) {
        cases.push_back(candidate);
    }
}

[[nodiscard]] std::vector<BenchmarkCase> build_cases(const CliOptions& options) {
    std::vector<BenchmarkCase> cases;
    const std::vector<Scenario> scenarios = selected_scenarios(options);

    const bool custom_geometry = options.payload_overridden || options.capacity_overridden;
    if (options.preset == "quick" || custom_geometry) {
        for (const Scenario scenario : scenarios) {
            add_case(cases,
                     {scenario, options.benchmark.capacity_bytes, options.benchmark.payload_bytes});
        }
        return cases;
    }

    for (const Scenario scenario : scenarios) {
        if (scenario == Scenario::transfer_retry) {
            constexpr std::size_t kMainCapacity = 64U * 1024U;
            for (const std::size_t payload : {0U, 16U, 64U, 256U, 1024U, 8192U}) {
                add_case(cases, {scenario, kMainCapacity, payload});
            }
            add_case(cases, {scenario, 16U * 1024U, 64U});
            add_case(cases, {scenario, 1024U * 1024U, 64U});
        } else if (scenario == Scenario::drop_new) {
            for (const std::size_t capacity : {16U * 1024U, 64U * 1024U, 1024U * 1024U}) {
                add_case(cases, {scenario, capacity, 64U});
            }
        } else if (scenario == Scenario::full_fast_fail) {
            add_case(cases, {scenario, 64U * 1024U, 64U});
        } else if (scenario == Scenario::write_success || scenario == Scenario::prefilled_read) {
            add_case(cases, {scenario, 1024U * 1024U, 64U});
        }
    }
    return cases;
}

[[nodiscard]] std::vector<Implementation> selected_implementations(const CliOptions& options) {
    if (options.implementation == "qlog") {
        return {Implementation::qlog};
    }
    if (options.implementation == "bqlog") {
#if QLOG_BENCH_WITH_BQLOG
        return {Implementation::bqlog};
#else
        throw std::invalid_argument("this binary was built without BQLog support");
#endif
    }

    std::vector<Implementation> implementations{Implementation::qlog};
#if QLOG_BENCH_WITH_BQLOG
    implementations.push_back(Implementation::bqlog);
#endif
    return implementations;
}

void resolve_affinity(BenchmarkOptions& options) {
    const std::vector<int> cpus = allowed_cpus();
    if (cpus.size() < 2U) {
        throw std::runtime_error("benchmark needs at least two allowed CPUs");
    }

    const auto is_allowed = [&cpus](int cpu) {
        return std::find(cpus.begin(), cpus.end(), cpu) != cpus.end();
    };

    if (options.producer_cpu < 0) {
        options.producer_cpu = cpus.front();
    }
    if (!is_allowed(options.producer_cpu)) {
        throw std::runtime_error("producer CPU is outside the process affinity mask");
    }

    if (options.consumer_cpu < 0) {
        const auto candidate = std::find_if(
            cpus.begin(), cpus.end(), [&options](int cpu) { return cpu != options.producer_cpu; });
        options.consumer_cpu = *candidate;
    }
    if (!is_allowed(options.consumer_cpu)) {
        throw std::runtime_error("consumer CPU is outside the process affinity mask");
    }
    if (options.producer_cpu == options.consumer_cpu) {
        throw std::runtime_error("producer and consumer CPUs must differ");
    }
}

[[nodiscard]] bool contains_implementation(const std::vector<Implementation>& implementations,
                                           Implementation candidate) {
    return std::find(implementations.begin(), implementations.end(), candidate) !=
           implementations.end();
}

void validate_case_geometry(const BenchmarkCase& benchmark_case, bool bqlog_selected) {
    const std::size_t capacity = benchmark_case.capacity_bytes;
    const std::size_t payload = benchmark_case.payload_bytes;

    if (capacity < 16U) {
        throw std::invalid_argument("capacity must be at least 16 bytes");
    }
    if ((capacity & (capacity - 1U)) != 0U) {
        throw std::invalid_argument("capacity must be a power of two");
    }
    if (capacity > (std::size_t{1} << 31U)) {
        throw std::invalid_argument("capacity must be at most 2^31 bytes");
    }

    const std::size_t configured_payload = std::max<std::size_t>(payload, 1U);
    if (configured_payload > capacity / 2U) {
        throw std::invalid_argument("payload must be at most capacity / 2");
    }
    if (bqlog_selected && capacity < 256U) {
        throw std::invalid_argument(
            "BQLog comparison requires at least 256 bytes of usable capacity");
    }
}

[[nodiscard]] std::vector<std::size_t> implementation_order(std::size_t count, bool reverse_order) {
    std::vector<std::size_t> order;
    order.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        order.push_back(index);
    }
    if (reverse_order) {
        std::reverse(order.begin(), order.end());
    }
    return order;
}

[[nodiscard]] std::string json_escape(std::string_view input) {
    std::string escaped;
    escaped.reserve(input.size());
    for (const char character : input) {
        switch (character) {
            case '\\':
                escaped += "\\\\";
                break;
            case '"':
                escaped += "\\\"";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped += character;
                break;
        }
    }
    return escaped;
}

void print_metadata(std::ostream& output, OutputFormat format, const BenchmarkOptions& options) {
    const bool reproducible = QLOG_BENCH_QLOG_DIRTY == 0 && QLOG_BENCH_BQLOG_DIRTY == 0;
    if (format == OutputFormat::jsonl) {
        output << "{\"type\":\"metadata\",\"schema\":\"qlog.spsc-bench.v1\""
               << ",\"qlog_commit\":\"" << QLOG_BENCH_QLOG_COMMIT << "\""
               << ",\"qlog_dirty\":" << QLOG_BENCH_QLOG_DIRTY << ",\"bqlog_commit\":\""
               << QLOG_BENCH_BQLOG_COMMIT << "\"" << ",\"bqlog_dirty\":" << QLOG_BENCH_BQLOG_DIRTY
               << ",\"reproducible\":" << (reproducible ? "true" : "false") << ",\"compiler\":\""
               << json_escape(__VERSION__) << "\"" << ",\"kernel\":\""
               << json_escape(kernel_description()) << "\"" << ",\"cpu_model\":\""
               << json_escape(cpu_model_name()) << "\""
               << ",\"producer_cpu\":" << options.producer_cpu
               << ",\"consumer_cpu\":" << options.consumer_cpu
               << ",\"warmup_ms\":" << options.warmup.count()
               << ",\"duration_ms\":" << options.duration.count()
               << ",\"repeat\":" << options.repeat << "}\n";
        return;
    }

    output << "# schema=qlog.spsc-bench.v1\n"
           << "# qlog_commit=" << QLOG_BENCH_QLOG_COMMIT << " dirty=" << QLOG_BENCH_QLOG_DIRTY
           << '\n'
           << "# bqlog_commit=" << QLOG_BENCH_BQLOG_COMMIT << " dirty=" << QLOG_BENCH_BQLOG_DIRTY
           << '\n'
           << "# result_class=" << (reproducible ? "REPRODUCIBLE" : "NON_REPRODUCIBLE") << '\n'
           << "# compiler=" << __VERSION__ << '\n'
           << "# kernel=" << kernel_description() << '\n'
           << "# cpu=" << cpu_model_name() << '\n'
           << "# producer_cpu=" << options.producer_cpu << " consumer_cpu=" << options.consumer_cpu
           << '\n'
           << "# warmup_ms=" << options.warmup.count()
           << " duration_ms=" << options.duration.count() << " repeat=" << options.repeat << "\n\n"
           << std::left << std::setw(7) << "impl" << std::setw(18) << "scenario" << std::right
           << std::setw(10) << "capacity" << std::setw(9) << "payload" << std::setw(5) << "rep"
           << std::setw(14) << "ops/s" << std::setw(13) << "full/drop" << std::setw(12) << "drop%"
           << std::setw(12) << "drain_us" << std::setw(8) << "valid" << '\n';
}

void print_sample(std::ostream& output, OutputFormat format, const BenchmarkResult& result) {
    const double drop_percent = result.counters.attempted_messages == 0U
                                    ? 0.0
                                    : static_cast<double>(result.counters.dropped_full) * 100.0 /
                                          static_cast<double>(result.counters.attempted_messages);

    if (format == OutputFormat::jsonl) {
        output << "{\"type\":\"sample\",\"impl\":\"" << to_string(result.implementation)
               << "\",\"scenario\":\"" << to_string(result.scenario)
               << "\",\"capacity_bytes\":" << result.options.capacity_bytes
               << ",\"payload_bytes\":" << result.options.payload_bytes
               << ",\"repetition\":" << result.repetition
               << ",\"active_elapsed_ns\":" << result.active_elapsed_ns
               << ",\"drain_elapsed_ns\":" << result.drain_elapsed_ns
               << ",\"total_elapsed_ns\":" << result.total_elapsed_ns
               << ",\"cycle_count\":" << result.cycle_count
               << ",\"target_records_per_cycle\":" << result.target_records_per_cycle
               << ",\"padding_records_per_cycle\":" << result.padding_records_per_cycle
               << ",\"rate_metric\":\"" << primary_rate_metric(result.scenario) << "\""
               << ",\"reserve_calls\":" << result.counters.reserve_calls
               << ",\"attempted_messages\":" << result.counters.attempted_messages
               << ",\"accepted\":" << result.counters.accepted
               << ",\"consumed\":" << result.counters.consumed
               << ",\"full_retries\":" << result.counters.full_retries
               << ",\"dropped_full\":" << result.counters.dropped_full
               << ",\"unexpected_failures\":" << result.counters.unexpected_failures
               << ",\"empty_reads\":" << result.counters.empty_reads
               << ",\"read_calls\":" << result.counters.read_calls
               << ",\"validation_failures\":" << result.counters.validation_failures
               << ",\"accepted_per_second\":" << result.accepted_per_second()
               << ",\"read_per_second\":" << result.read_per_second()
               << ",\"primary_per_second\":" << result.primary_per_second()
               << ",\"nanoseconds_per_operation\":" << result.nanoseconds_per_operation()
               << ",\"processed_per_second\":" << result.processed_per_second()
               << ",\"payload_mib_per_second\":" << result.payload_mib_per_second()
               << ",\"full_calls_per_second\":" << result.full_calls_per_second()
               << ",\"checksum\":" << result.counters.checksum
               << ",\"affinity_applied\":" << (result.affinity_applied ? "true" : "false")
               << ",\"valid\":" << (result.valid ? "true" : "false") << ",\"error\":\""
               << json_escape(result.error) << "\"}\n";
        return;
    }

    const std::uint64_t full_or_drop = result.counters.full_retries + result.counters.dropped_full;
    output << std::left << std::setw(7) << to_string(result.implementation) << std::setw(18)
           << to_string(result.scenario) << std::right << std::setw(10)
           << result.options.capacity_bytes << std::setw(9) << result.options.payload_bytes
           << std::setw(5) << result.repetition << std::setw(14) << std::fixed
           << std::setprecision(0) << result.primary_per_second() << std::setw(13) << full_or_drop
           << std::setw(12) << std::setprecision(3) << drop_percent << std::setw(12)
           << std::setprecision(1) << static_cast<double>(result.drain_elapsed_ns) / 1000.0
           << std::setw(8) << (result.valid ? "yes" : "NO") << '\n';

    if (!result.error.empty()) {
        output << "  error: " << result.error << '\n';
    }
}

[[nodiscard]] double median(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const std::size_t middle = values.size() / 2U;
    if ((values.size() & 1U) != 0U) {
        return values[middle];
    }
    return (values[middle - 1U] + values[middle]) / 2.0;
}

void print_summary(std::ostream& output, OutputFormat format,
                   const std::vector<BenchmarkResult>& results) {
    if (results.empty()) {
        return;
    }

    std::vector<double> rates;
    rates.reserve(results.size());
    for (const BenchmarkResult& result : results) {
        rates.push_back(result.primary_per_second());
    }
    const double median_rate = median(rates);

    std::vector<double> deviations;
    deviations.reserve(rates.size());
    for (const double rate : rates) {
        deviations.push_back(std::abs(rate - median_rate));
    }
    const double mad = median(deviations);
    const BenchmarkResult& first = results.front();
    const bool all_valid = std::all_of(results.begin(), results.end(),
                                       [](const BenchmarkResult& result) { return result.valid; });

    if (format == OutputFormat::jsonl) {
        output << "{\"type\":\"summary\",\"impl\":\"" << to_string(first.implementation)
               << "\",\"scenario\":\"" << to_string(first.scenario)
               << "\",\"capacity_bytes\":" << first.options.capacity_bytes
               << ",\"payload_bytes\":" << first.options.payload_bytes
               << ",\"samples\":" << results.size() << ",\"rate_metric\":\""
               << primary_rate_metric(first.scenario) << "\"" << ",\"median_rate\":" << median_rate
               << ",\"mad_rate\":" << mad << ",\"valid\":" << (all_valid ? "true" : "false")
               << "}\n";
        return;
    }

    output << "  rate_metric=" << primary_rate_metric(first.scenario)
           << ", median_rate=" << std::fixed << std::setprecision(0) << median_rate
           << "/s, MAD=" << mad << "/s\n";
}

[[nodiscard]] BenchmarkResult run_once(Implementation implementation, Scenario scenario,
                                       const BenchmarkOptions& options, std::size_t repetition) {
    if (implementation == Implementation::qlog) {
        return run_qlog_benchmark(scenario, options, repetition);
    }
#if QLOG_BENCH_WITH_BQLOG
    return run_bqlog_benchmark(scenario, options, repetition);
#else
    throw std::runtime_error("BQLog support is disabled");
#endif
}

}  // namespace

int benchmark_main(int argc, char** argv) {
    const CliOptions cli = parse_cli(argc, argv);
    if (cli.help) {
        print_help(std::cout);
        return 0;
    }

    BenchmarkOptions base_options = cli.benchmark;
    resolve_affinity(base_options);
    const std::vector<BenchmarkCase> cases = build_cases(cli);
    const std::vector<Implementation> implementations = selected_implementations(cli);
    const bool bqlog_selected = contains_implementation(implementations, Implementation::bqlog);
    for (const BenchmarkCase& benchmark_case : cases) {
        validate_case_geometry(benchmark_case, bqlog_selected);
    }

    std::ofstream output_file;
    std::ostream* output = &std::cout;
    if (!cli.output_path.empty()) {
        output_file.open(cli.output_path, std::ios::out | std::ios::trunc);
        if (!output_file) {
            throw std::runtime_error("failed to open output file");
        }
        output = &output_file;
    }

    print_metadata(*output, cli.output_format, base_options);
    bool all_valid = true;

    for (std::size_t case_index = 0; case_index < cases.size(); ++case_index) {
        const BenchmarkCase& benchmark_case = cases[case_index];
        BenchmarkOptions options = base_options;
        options.capacity_bytes = benchmark_case.capacity_bytes;
        options.payload_bytes = benchmark_case.payload_bytes;

        if (options.warmup.count() > 0) {
            const std::vector<std::size_t> warmup_order =
                implementation_order(implementations.size(), (case_index & 1U) != 0U);
            for (const std::size_t implementation_index : warmup_order) {
                BenchmarkOptions warmup_options = options;
                warmup_options.duration = options.warmup;
                const BenchmarkResult warmup =
                    run_once(implementations[implementation_index], benchmark_case.scenario,
                             warmup_options, 0U);
                if (!warmup.valid) {
                    throw std::runtime_error(std::string("warmup failed for ") +
                                             to_string(implementations[implementation_index]) +
                                             "/" + to_string(benchmark_case.scenario) + ": " +
                                             warmup.error);
                }
            }
        }

        std::vector<std::vector<BenchmarkResult>> results_by_implementation(implementations.size());
        for (std::vector<BenchmarkResult>& results : results_by_implementation) {
            results.reserve(options.repeat);
        }

        for (std::size_t repetition = 0; repetition < options.repeat; ++repetition) {
            const std::vector<std::size_t> order = implementation_order(
                implementations.size(), ((case_index + repetition) & 1U) != 0U);
            for (const std::size_t implementation_index : order) {
                BenchmarkResult result = run_once(implementations[implementation_index],
                                                  benchmark_case.scenario, options, repetition);
                all_valid = all_valid && result.valid;
                print_sample(*output, cli.output_format, result);
                results_by_implementation[implementation_index].push_back(std::move(result));
            }
        }

        for (const std::vector<BenchmarkResult>& results : results_by_implementation) {
            print_summary(*output, cli.output_format, results);
        }
    }

    return all_valid ? 0 : 1;
}

}  // namespace qlog::bench

int main(int argc, char** argv) {
    try {
        return qlog::bench::benchmark_main(argc, argv);
    } catch (const std::exception& exception) {
        std::cerr << "benchmark error: " << exception.what() << '\n';
        return 2;
    }
}
