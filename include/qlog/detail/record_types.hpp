#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

#include "argument_tag.hpp"
#include "record_header.hpp"
#include "record_limits.hpp"

namespace qlog::detail {
struct RecordMetadata final {
    std::uint64_t time_value{};
    std::uint32_t category_id{};
    std::uint8_t level{};
    std::uint8_t flags{};
};

class RecordValidationPolicy final {
   public:
    explicit constexpr RecordValidationPolicy(std::array<std::uint64_t, 4> valid_levels,
                                              bool fallback_timestamp_allowed) noexcept
        : valid_levels_(valid_levels), fallback_timestamp_allowed_(fallback_timestamp_allowed) {}

    [[nodiscard]] constexpr bool allows_level(std::uint8_t level) const noexcept {
        const auto index = static_cast<std::size_t>(level);
        return (((valid_levels_[index >> 6U]) & (std::uint64_t{1} << (index & 63U))) != 0);
    }
    [[nodiscard]] constexpr bool fallback_timestamp_allowed() const noexcept {
        return fallback_timestamp_allowed_;
    }

   private:
    std::array<std::uint64_t, 4> valid_levels_;
    bool fallback_timestamp_allowed_;
};

enum class MetadataError : std::uint8_t {
    invalid_level,
    unknown_flags,
    reserved_timestamp_status,
    invalid_time_value,
    fallback_timestamp_not_configured,
};

namespace record_metadata_impl {
[[nodiscard]] inline std::optional<MetadataError> validate_record_metadata(
    const RecordMetadata& metadata, const RecordValidationPolicy& policy) noexcept {
    if (!policy.allows_level(metadata.level)) {
        return MetadataError::invalid_level;
    }
    constexpr auto unknown_mask = static_cast<std::uint8_t>(~kKnownFlagMask);
    if ((metadata.flags & unknown_mask) != 0U) {
        return MetadataError::unknown_flags;
    }
    const auto status = metadata.flags & kTimestampStatusMask;
    if (status == 3U) {
        return MetadataError::reserved_timestamp_status;
    }
    if (status == 2U && metadata.time_value != 0U) {
        return MetadataError::invalid_time_value;
    }
    if (status == 1U && !policy.fallback_timestamp_allowed()) {
        return MetadataError::fallback_timestamp_not_configured;
    }
    return std::nullopt;
}
}  // namespace record_metadata_impl

enum class EncodeError : std::uint8_t {
    invalid_destination_metadata,
    destination_size_mismatch,
    invalid_level,
    unknown_flags,
    reserved_timestamp_status,
    invalid_time_value,
    fallback_timestamp_not_configured,
};

enum class DecodeError : std::uint8_t {
    invalid_payload_metadata,
    invalid_workspace,
    record_too_small,
    invalid_level,
    unknown_flags,
    reserved_timestamp_status,
    invalid_time_value,
    fallback_timestamp_not_configured,
    format_too_large,
    invalid_format_length,
    invalid_args_length,
    arg_count_exceeded,
    invalid_or_unknown_tag,
    invalid_bool,
    truncated_value,
    truncated_string_length,
    truncated_string,
    decoded_count_mismatch,
    trailing_args_bytes,
};

struct DecodeFailure final {
    DecodeError error;
    std::size_t error_offset;
    std::uint8_t argument_index;  // 0xFF = non-argument error.
};

struct DecodedArg final {
    union Value {
        std::uint64_t bits;
        const std::byte* bytes;
    };

    Value value{.bits = 0U};
    std::uint32_t byte_count{};
    ArgumentTag tag{ArgumentTag::Invalid};
};

static_assert(sizeof(DecodedArg) == 16U);
static_assert(alignof(DecodedArg) == 8U);
static_assert(std::is_standard_layout_v<DecodedArg>);
static_assert(std::is_trivially_copyable_v<DecodedArg>);

struct RecordCodecAccess;

// 成功视图
class DecodedRecordView final {
   public:
    [[nodiscard]] const RecordHeader& header() const noexcept {
        return header_;
    }
    [[nodiscard]] const std::byte* format_data() const noexcept {
        return format_data_;
    }
    [[nodiscard]] std::uint32_t format_size() const noexcept {
        return header_.format_bytes;
    }
    [[nodiscard]] const DecodedArg* arguments_data() const noexcept {
        return arguments_;
    }
    [[nodiscard]] std::uint16_t argument_count() const noexcept {
        return header_.arg_count;
    }

   private:
    DecodedRecordView(RecordHeader header, const std::byte* format_data,
                      const DecodedArg* arguments) noexcept
        : header_(header), format_data_(format_data), arguments_(arguments) {}
    RecordHeader header_;
    const std::byte* format_data_;
    const DecodedArg* arguments_;
    friend struct RecordCodecAccess;
};

class EncodeResult final {
   public:
    [[nodiscard]] bool succeeded() const noexcept {
        return error_ == std::nullopt ? true : false;
    }
    [[nodiscard]] std::uint32_t bytes_written() const noexcept {
        return bytes_written_;
    }
    [[nodiscard]] const EncodeError* failure() const noexcept {
        return succeeded() ? nullptr : &*error_;
    }

   private:
    explicit EncodeResult(std::uint32_t bytes_written) noexcept
        : bytes_written_(bytes_written), error_(std::nullopt) {}
    explicit EncodeResult(EncodeError error) noexcept : bytes_written_(0), error_(error) {}

    std::uint32_t bytes_written_;
    std::optional<EncodeError> error_;
    friend struct RecordCodecAccess;
};

class DecodeResult final {
   public:
    [[nodiscard]] bool succeeded() const noexcept {
        return record_.has_value() ? true : false;
    }
    [[nodiscard]] const DecodedRecordView* record() const noexcept {
        return succeeded() ? &*record_ : nullptr;
    }
    [[nodiscard]] const DecodeFailure* failure() const noexcept {
        return succeeded() ? nullptr : &failure_;
    }

   private:
    explicit DecodeResult(DecodedRecordView record) noexcept : record_(record), failure_{} {}

    explicit DecodeResult(DecodeFailure failure) noexcept
        : record_(std::nullopt), failure_(failure) {}
    std::optional<DecodedRecordView> record_;
    DecodeFailure failure_;
    friend struct RecordCodecAccess;
};

struct RecordCodecAccess final {
    [[nodiscard]] static inline EncodeResult encode_success(std::uint32_t bytes) noexcept {
        return EncodeResult(bytes);
    }
    [[nodiscard]] static inline EncodeResult encode_failure(EncodeError error) noexcept {
        return EncodeResult(error);
    }
    [[nodiscard]] static inline DecodeResult decode_success(RecordHeader header,
                                                            const std::byte* format_data,
                                                            const DecodedArg* arguments) noexcept {
        return DecodeResult(DecodedRecordView(header, format_data, arguments));
    }
    [[nodiscard]] static inline DecodeResult decode_failure(DecodeFailure failure) noexcept {
        return DecodeResult(DecodeFailure(failure));
    }
};

}  // namespace qlog::detail