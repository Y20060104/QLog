#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace qlog {
enum class FlushMode : std::uint8_t { buffered, durable };
enum class ResetMode : std::uint8_t { reuse_compatible, recreate_all };
enum class SubmitStatus : std::uint8_t {
    submitted,
    busy,
    invalid_config,
    resource_exhausted,
    open_failed,
    identity_exhausted,
    stopped
};
enum class PollStatus : std::uint8_t { pending, completed, unknown_request };
enum class CompletionStatus : std::uint8_t {
    applied,
    applied_with_io_error,
    completed,
    completed_with_io_error,
    backend_failed
};
enum class IoStage : std::uint8_t { open, write, sync, close };
enum class ConfigError : std::uint8_t {
    invalid_pointer,
    empty_name,
    duplicate_name,
    invalid_type,
    invalid_level_bits,
    category_count_mismatch,
    invalid_category_value,
    invalid_output_config,
    invalid_backend_config,
    invalid_path
};

struct IoFailure {
    std::string appender_name;
    IoStage stage{};
    int system_error{0};
    std::uint64_t unwritten_bytes{0};
    bool durability_uncertain{false};
    std::string output_path;
};

struct SubmitResult {
    SubmitStatus status{};
    std::uint64_t request_id{0};
    std::optional<IoFailure> error;
    // reason/index are meaningful only when status == invalid_config.
    ConfigError reason{};
    std::size_t config_index{std::numeric_limits<std::size_t>::max()};
};

struct ManagementCompletion {
    std::uint64_t request_id{0};
    CompletionStatus status{};
    std::vector<IoFailure> errors;
};

struct PollResult {
    PollStatus status{};
    std::optional<ManagementCompletion> completion;
};

struct RetiredIoSummary {
    std::uint64_t event_count{0};
    std::uint64_t lost_bytes{0};
    std::optional<IoFailure> first_error;
};

struct ShutdownResult {
    std::vector<IoFailure> errors;
    bool backend_failed{false};
    bool drain_incomplete{false};
    RetiredIoSummary retired_io;
};
}  // namespace qlog
