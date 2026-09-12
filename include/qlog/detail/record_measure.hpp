#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>

#include "argument_traits.hpp"
#include "checked_size.hpp"
#include "record_limits.hpp"

namespace qlog::detail {

struct FormatInput final {
    const std::byte* data{};
    std::size_t size{};
    std::uint64_t precomputed_stored_hash{};  // 0 means hash while encoding.
};

enum class MeasureError : std::uint8_t {
    invalid_limits,
    invalid_format_metadata,
    format_too_large,
    invalid_string_metadata,
    argument_length_out_of_range,
    args_length_out_of_range,
    size_overflow,
    record_length_out_of_range,
    payload_too_large,
};

struct MeasureFailure final {
    MeasureError error;
    std::uint8_t argument_index;  // 0xFF for a non-argument failure.
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

    std::optional<PreparedRecord<N>> prepared_;
    MeasureFailure failure_{};
    friend RecordMeasureAccess;
};

struct RecordMeasureAccess final {
   private:
    template <std::size_t N>
    [[nodiscard]] static constexpr MeasureResult<N> failure(MeasureFailure failure) noexcept {
        return MeasureResult<N>{failure};
    }

    template <std::size_t N>
    [[nodiscard]] static constexpr MeasureResult<N> success(
        const std::byte* format_data, std::uint64_t precomputed_stored_hash,
        std::uint32_t format_size, std::uint32_t args_size, std::uint32_t payload_size,
        std::uint32_t max_payload_size, std::array<NormalizedArgument, N> arguments) noexcept {
        return MeasureResult<N>{PreparedRecord<N>{format_data, precomputed_stored_hash, format_size,
                                                  args_size, payload_size, max_payload_size,
                                                  std::move(arguments)}};
    }

    template <typename... Args>
        requires(sizeof...(Args) <= kMaxArgCount) && (SupportedArgument<Args> && ...)
    friend MeasureResult<sizeof...(Args)> measure_record(FormatInput, std::size_t,
                                                         Args&&...) noexcept;
};

namespace record_measure_impl {

inline constexpr std::uint8_t kNoArgument = std::numeric_limits<std::uint8_t>::max();

struct State final {
    NormalizedArgument* arguments{};
    MeasureFailure failure{};
};

[[nodiscard]] inline bool store_utf8(const std::byte* pointer, std::size_t length,
                                     std::size_t argument_index, NormalizedArgument& output,
                                     MeasureFailure& failure) noexcept {
    if (length != 0U && pointer == nullptr) {
        failure = {
            MeasureError::invalid_string_metadata,
            static_cast<std::uint8_t>(argument_index),
            length,
        };
        return false;
    }

    if (length > std::numeric_limits<std::uint32_t>::max()) {
        failure = {
            MeasureError::argument_length_out_of_range,
            static_cast<std::uint8_t>(argument_index),
            length,
        };
        return false;
    }

    output = NormalizedArgument{
        .bytes = pointer,
        .byte_count = static_cast<std::uint32_t>(length),
        .tag = ArgumentTag::Utf8String,
    };
    return true;
}

template <class T>
bool normalize_one(State& state, std::size_t index, const T& value) noexcept {
    using Ref = decltype(value);
    using Raw = std::remove_reference_t<Ref>;
    using U = std::remove_cv_t<Raw>;

    constexpr ArgumentKind kind = ArgumentTraits<Ref>::kind;
    auto& output = state.arguments[index];

    if constexpr (kind == ArgumentKind::Bool) {
        output = NormalizedArgument{
            .bits = value ? std::uint64_t{1} : std::uint64_t{0},
            .tag = ArgumentTag::Bool,
        };
        return true;
    } else if constexpr (kind == ArgumentKind::Char) {
        const auto byte = static_cast<unsigned char>(value);

        output = NormalizedArgument{
            .bits = static_cast<std::uint64_t>(byte),
            .tag = ArgumentTag::Char,
        };
        return true;
    } else if constexpr (kind == ArgumentKind::Int8 || kind == ArgumentKind::Int16 ||
                         kind == ArgumentKind::Int32 || kind == ArgumentKind::Int64 ||
                         kind == ArgumentKind::UInt8 || kind == ArgumentKind::UInt16 ||
                         kind == ArgumentKind::UInt32 || kind == ArgumentKind::UInt64) {
        if constexpr (std::is_enum_v<U>) {
            using Underlying = std::underlying_type_t<U>;
            using Unsigned = std::make_unsigned_t<Underlying>;

            const auto underlying = static_cast<Underlying>(value);
            const auto unsigned_value = static_cast<Unsigned>(underlying);

            output = NormalizedArgument{
                .bits = static_cast<std::uint64_t>(unsigned_value),
                .tag = wire_tag_for(kind),
            };
            return true;
        } else {
            using Unsigned = std::make_unsigned_t<U>;

            output = NormalizedArgument{
                .bits = static_cast<std::uint64_t>(static_cast<Unsigned>(value)),
                .tag = wire_tag_for(kind),
            };
            return true;
        }
    } else if constexpr (kind == ArgumentKind::F32) {
        output = NormalizedArgument{
            .bits = static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(value)),
            .tag = ArgumentTag::F32,
        };
        return true;
    } else if constexpr (kind == ArgumentKind::F64) {
        output = NormalizedArgument{
            .bits = std::bit_cast<std::uint64_t>(value),
            .tag = ArgumentTag::F64,
        };
        return true;
    } else if constexpr (kind == ArgumentKind::Pointer64) {
        if constexpr (std::is_same_v<U, std::nullptr_t>) {
            output = NormalizedArgument{
                .bits = 0U,
                .tag = ArgumentTag::Pointer64,
            };
        } else {
            output = NormalizedArgument{
                .bits = value.value,
                .tag = ArgumentTag::Pointer64,
            };
        }
        return true;
    } else if constexpr (kind == ArgumentKind::Utf8String) {
        if constexpr (std::is_array_v<Raw>) {
            constexpr std::size_t extent = std::extent_v<Raw>;
            constexpr std::size_t length = extent - 1U;

            if (value[extent - 1U] != 0) {
                state.failure = {MeasureError::invalid_string_metadata,
                                 static_cast<std::uint8_t>(index), length};
                return false;
            }

            return store_utf8(reinterpret_cast<const std::byte*>(value), length, index, output,
                              state.failure);
        } else {
            return store_utf8(reinterpret_cast<const std::byte*>(value.data()), value.size(), index,
                              output, state.failure);
        }

    } else if constexpr (kind == ArgumentKind::CStr) {
        if (value.data == nullptr) {
            output = NormalizedArgument{
                .tag = ArgumentTag::NullUtf8,
            };
            return true;
        }

        const std::size_t length = std::strlen(value.data);

        return store_utf8(reinterpret_cast<const std::byte*>(value.data), length, index, output,
                          state.failure);
    }

    return false;
}

template <std::size_t index>
[[nodiscard]] bool normalize_pack(State&) noexcept {
    return true;
}

template <std::size_t Index, class First, class... Rest>
[[nodiscard]] bool normalize_pack(State& state, const First& first, const Rest&... rest) noexcept {
    if (!normalize_one(state, Index, first)) {
        return false;
    }

    return normalize_pack<Index + 1U>(state, rest...);
}

[[nodiscard]] inline bool measure_sizes(const NormalizedArgument* arguments,
                                        std::size_t argument_count, std::size_t format_size,
                                        std::size_t max_payload_size, std::size_t& args_size,
                                        std::size_t& payload_size,
                                        MeasureFailure& failure) noexcept {
    args_size = 0U;

    for (std::size_t index = 0U; index < argument_count; ++index) {
        std::size_t argument_size = 0U;

        if (!encoded_size(arguments[index], argument_size)) {
            return false;
        }

        std::size_t new_args_size = 0U;
        if (!checked_add(args_size, argument_size, new_args_size)) {
            failure = {
                MeasureError::size_overflow,
                kNoArgument,
                argument_size,
            };
            return false;
        }
        args_size = new_args_size;
    }

    if (args_size > std::numeric_limits<std::uint32_t>::max()) {
        failure = {
            MeasureError::args_length_out_of_range,
            kNoArgument,
            args_size,
        };
        return false;
    }

    std::size_t header_and_format = 0U;
    if (!checked_add(kRecordHeaderBytes, format_size, header_and_format)) {
        failure = {
            MeasureError::size_overflow,
            kNoArgument,
            format_size,
        };
        return false;
    }

    if (!checked_add(header_and_format, args_size, payload_size)) {
        failure = {
            MeasureError::size_overflow,
            kNoArgument,
            args_size,
        };
        return false;
    }

    if (payload_size > std::numeric_limits<std::uint32_t>::max()) {
        failure = {
            MeasureError::record_length_out_of_range,
            kNoArgument,
            payload_size,
        };
        return false;
    }

    if (payload_size > max_payload_size) {
        failure = {
            MeasureError::payload_too_large,
            kNoArgument,
            payload_size,

        };
        return false;
    }

    return true;
}

}  // namespace record_measure_impl

template <typename... Args>
    requires(sizeof...(Args) <= kMaxArgCount) && (SupportedArgument<Args> && ...)
[[nodiscard]] MeasureResult<sizeof...(Args)> measure_record(FormatInput format,
                                                            std::size_t max_payload_size,
                                                            Args&&... arguments) noexcept {
    constexpr std::size_t N = sizeof...(Args);

    if (max_payload_size < kRecordHeaderBytes ||
        max_payload_size > std::numeric_limits<std::uint32_t>::max()) {
        return RecordMeasureAccess::failure<N>({MeasureError::invalid_limits,
                                                std::numeric_limits<std::uint8_t>::max(),
                                                max_payload_size});
    }

    if (format.size != 0 && format.data == nullptr) {
        return RecordMeasureAccess::failure<N>({MeasureError::invalid_format_metadata,
                                                std::numeric_limits<std::uint8_t>::max(),
                                                format.size});
    }

    if (format.size > kMaxFormatBytes) {
        return RecordMeasureAccess::failure<N>({MeasureError::format_too_large,
                                                std::numeric_limits<std::uint8_t>::max(),
                                                format.size});
    }

    std::array<NormalizedArgument, N> normalized_arguments{};

    record_measure_impl::State state{
        normalized_arguments.data(),
        {},
    };

    if (!record_measure_impl::normalize_pack<0U>(state, arguments...)) {
        return RecordMeasureAccess::failure<N>(state.failure);
    }

    std::size_t args_size = 0U;
    std::size_t payload_size = 0U;

    if (!record_measure_impl::measure_sizes(normalized_arguments.data(), N, format.size,
                                            max_payload_size, args_size, payload_size,
                                            state.failure)) {
        return RecordMeasureAccess::failure<N>(state.failure);
    }

    return RecordMeasureAccess::success<N>(
        format.data, format.precomputed_stored_hash, static_cast<std::uint32_t>(format.size),
        static_cast<std::uint32_t>(args_size), static_cast<std::uint32_t>(payload_size),
        static_cast<std::uint32_t>(max_payload_size), std::move(normalized_arguments));
}

}  // namespace qlog::detail
