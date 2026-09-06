#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <tuple>
#include <utility>

#include "argument_traits.hpp"
#include "checked_size.hpp"
#include "record_limits.hpp"

namespace qlog::detail {
struct FormatInput final {
    const std::byte* data{};
    std::size_t size{};
    std::uint64_t precomputed_stored_hash{};  // 0表示运行时计算
};
enum class MeasureError : std::uint8_t {
    invalid_limits,
    invalid_format_metadata,
    format_too_large,
    invalid_string_metadata,
    invalid_cstr,
    argument_length_out_of_range,
    args_length_out_of_range,
    size_overflow,
    record_length_out_of_range,
    payload_too_large,
};

struct MeasureFailure final {
    MeasureError error;
    std::uint8_t argument_index;  // 非参数错误用0xFF
    std::size_t byte_count;
};

struct RecordMeasureAccess;

template <std::size_t N>
class PreparedRecord final {
    static_assert(N <= kMaxArgCount);

   public:
    [[nodiscard]] const std::byte* format_data() const noexcept {
        return format_data_;
    }

    [[nodiscard]] std::uint32_t format_size() const noexcept {
        return format_size_;
    }

    [[nodiscard]] std::uint64_t precomputed_stored_hash() const noexcept {
        return precomputed_stored_hash_;
    }

    [[nodiscard]] const NormalizedArgument* arguments_data() const noexcept {
        return arguments_.data();
    }

    [[nodiscard]] constexpr std::size_t argument_count() const noexcept {
        return N;
    }

    [[nodiscard]] std::uint32_t args_size() const noexcept {
        return args_size_;
    }

    [[nodiscard]] std::uint32_t payload_size() const noexcept {
        return payload_size_;
    }

    [[nodiscard]] std::uint32_t max_payload_size() const noexcept {
        return max_payload_size_;
    }

   private:
    friend struct RecordMeasureAccess;

    constexpr PreparedRecord(const std::byte* format_data, std::uint64_t precomputed_stored_hash,
                             std::uint32_t format_size, std::uint32_t args_size,
                             std::uint32_t payload_size, std::uint32_t max_payload_size,
                             std::array<NormalizedArgument, N> arguments) noexcept
        : format_data_(format_data),
          precomputed_stored_hash_(precomputed_stored_hash),
          format_size_(format_size),
          args_size_(args_size),
          payload_size_(payload_size),
          max_payload_size_(max_payload_size),
          arguments_(std::move(arguments)) {}

    const std::byte* format_data_{};
    std::uint64_t precomputed_stored_hash_{};

    std::uint32_t format_size_{};
    std::uint32_t args_size_{};
    std::uint32_t payload_size_{};
    std::uint32_t max_payload_size_{};

    std::array<NormalizedArgument, N> arguments_{};
};

template <std::size_t N>
class MeasureResult final {
   public:
    [[nodiscard]] bool succeeded() const noexcept {
        return prepared_.has_value();
    }

    [[nodiscard]] const PreparedRecord<N>* prepared() const noexcept {
        return prepared_.has_value() ? &*prepared_ : nullptr;
    }

    [[nodiscard]] const MeasureFailure* failure() const noexcept {
        return prepared_.has_value() ? nullptr : &failure_;
    }

   private:
    explicit constexpr MeasureResult(PreparedRecord<N> prepared) noexcept
        : prepared_(std::move(prepared)), failure_{} {}

    explicit constexpr MeasureResult(MeasureFailure failure) noexcept
        : prepared_(std::nullopt), failure_(failure) {}

   private:
    std::optional<PreparedRecord<N>> prepared_;
    MeasureFailure failure_{};
    friend RecordMeasureAccess;
};

template <typename... Args>
    requires(sizeof...(Args) <= kMaxArgCount) && (SupportedArgument<Args> && ...)
[[nodiscard]] MeasureResult<sizeof...(Args)> measure_record(FormatInput format,
                                                            std::size_t max_payload_size,
                                                            Args&&... arguments) noexcept {
    constexpr std::size_t N = sizeof...(Args);
}

}  // namespace qlog::detail