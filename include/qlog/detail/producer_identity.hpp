#pragma once

#include <cstdint>
#include <optional>
namespace qlog::detail {

[[nodiscard]] std::uint64_t allocate_logger_id();
[[nodiscard]] std::optional<std::uint64_t> allocate_producer_token() noexcept;

}  // namespace qlog::detail