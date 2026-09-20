#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace qlog::detail {
struct FormatSpec {
    bool used{false};
    bool upper{true};
    char fill{' '};
    char align{'>'};
    char sign{'-'};
    char prefix{' '};
    std::uint32_t offset{0};
    std::uint32_t width{0};
    std::uint32_t precision{0xFFFFFFFFU};
    char type{' '};
};

enum class FormatError : std::uint8_t {
    invalid_format_metadata,
    invalid_arguments,
    invalid_workspace,
    format_too_large,
    number_conversion_failed,
    text_output_limit_exceeded,
    time_conversion_failed,
};

struct FormatFailure {
    FormatError error;
    std::size_t byte_offset;
    std::uint8_t argument_index;
};

struct FormatResult {
    std::size_t size;
    std::optional<FormatFailure> failure;
};
}  // namespace qlog::detail