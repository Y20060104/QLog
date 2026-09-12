#pragma once
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <type_traits>
#include <utility>

#include "argument_tag.hpp"
#include "format_hash.hpp"
#include "record_header.hpp"
#include "record_limits.hpp"
#include "record_measure.hpp"
#include "record_types.hpp"

namespace qlog::detail::record_encoder_impl {
[[nodiscard]] inline EncodeError encode_error_from_metadata(MetadataError error) noexcept {
    switch (error) {
        case MetadataError::invalid_level:
            return EncodeError::invalid_level;

        case MetadataError::unknown_flags:
            return EncodeError::unknown_flags;
        case MetadataError::reserved_timestamp_status:
            return EncodeError::reserved_timestamp_status;
        case MetadataError::invalid_time_value:
            return EncodeError::invalid_time_value;
        case MetadataError::fallback_timestamp_not_configured:
            return EncodeError::fallback_timestamp_not_configured;
    }
    assert(false);
    std::terminate();
}
template <std::size_t Width>
inline void store_low_bits_le(std::byte* destination, std::uint64_t bits) noexcept {
    static_assert(Width == 1U || Width == 2U || Width == 4U || Width == 8U);
    if constexpr (Width == 1U) {
        const auto local = static_cast<std::uint8_t>(bits);
        std::memcpy(destination, &local, sizeof local);
    } else if constexpr (Width == 2U) {
        const auto local = static_cast<std::uint16_t>(bits);
        std::memcpy(destination, &local, sizeof local);
    } else if constexpr (Width == 4U) {
        const auto local = static_cast<std::uint32_t>(bits);
        std::memcpy(destination, &local, sizeof local);
    } else {
        const auto local = static_cast<std::uint64_t>(bits);
        std::memcpy(destination, &local, sizeof local);
    }
}

[[nodiscard]] inline std::byte* encode_argument_unchecked(
    std::byte* cursor, const NormalizedArgument& argument) noexcept {
    if (argument.tag == ArgumentTag::Bool) {
        *cursor++ = static_cast<std::byte>(argument.tag);
        store_low_bits_le<1U>(cursor, argument.bits);
        return cursor + 1U;
    } else if (argument.tag == ArgumentTag::Char) {
        *cursor++ = static_cast<std::byte>(argument.tag);
        store_low_bits_le<1U>(cursor, argument.bits);
        return cursor + 1U;
    } else if (argument.tag == ArgumentTag::Int8 || argument.tag == ArgumentTag::UInt8) {
        *cursor++ = static_cast<std::byte>(argument.tag);
        store_low_bits_le<1U>(cursor, argument.bits);
        return cursor + 1U;
    } else if (argument.tag == ArgumentTag::Int16 || argument.tag == ArgumentTag::UInt16) {
        *cursor++ = static_cast<std::byte>(argument.tag);
        store_low_bits_le<2U>(cursor, argument.bits);
        return cursor + 2U;
    } else if (argument.tag == ArgumentTag::Int32 || argument.tag == ArgumentTag::UInt32) {
        *cursor++ = static_cast<std::byte>(argument.tag);
        store_low_bits_le<4U>(cursor, argument.bits);
        return cursor + 4U;
    } else if (argument.tag == ArgumentTag::Int64 || argument.tag == ArgumentTag::UInt64) {
        *cursor++ = static_cast<std::byte>(argument.tag);
        store_low_bits_le<8U>(cursor, argument.bits);
        return cursor + 8U;
    } else if (argument.tag == ArgumentTag::F32) {
        *cursor++ = static_cast<std::byte>(argument.tag);
        store_low_bits_le<4U>(cursor, argument.bits);
        return cursor + 4U;
    } else if (argument.tag == ArgumentTag::F64) {
        *cursor++ = static_cast<std::byte>(argument.tag);
        store_low_bits_le<8U>(cursor, argument.bits);
        return cursor + 8U;
    } else if (argument.tag == ArgumentTag::Pointer64) {
        *cursor++ = static_cast<std::byte>(argument.tag);
        store_low_bits_le<8U>(cursor, argument.bits);
        return cursor + 8U;
    } else if (argument.tag == ArgumentTag::Utf8String) {
        *cursor++ = static_cast<std::byte>(argument.tag);
        store_low_bits_le<4U>(cursor, argument.byte_count);
        cursor += 4U;
        if (argument.byte_count != 0U) {
            std::memcpy(cursor, argument.bytes, argument.byte_count);
            cursor += argument.byte_count;
        }
        return cursor;
    } else if (argument.tag == ArgumentTag::NullUtf8) {
        *cursor++ = static_cast<std::byte>(argument.tag);
        return cursor;
    }
    // PreparedRecord can only contain tags admitted by checked measure.
    assert(false);
    std::terminate();
}

template <std::size_t N, std::size_t... I>
[[nodiscard]] inline std::byte* encode_arguments_unchecked(std::byte* cursor,
                                                           const PreparedRecord<N>& prepared,
                                                           std::index_sequence<I...>) noexcept {
    static_assert(sizeof...(I) == N);
    ((cursor = encode_argument_unchecked(cursor, prepared.arguments_data()[I])), ...);
    return cursor;
}

}  // namespace qlog::detail::record_encoder_impl

namespace qlog::detail {
template <std::size_t N>
[[nodiscard]] EncodeResult encode_v1(std::byte* destination, std::size_t destination_size,
                                     const PreparedRecord<N>& prepared,
                                     const RecordMetadata& metadata,
                                     const RecordValidationPolicy& policy,
                                     const FormatHashDispatch& hash_dispatch) noexcept {
    if (destination_size != 0U && destination == nullptr) {
        return RecordCodecAccess::encode_failure(EncodeError::invalid_destination_metadata);
    }
    if (destination_size != prepared.payload_size()) {
        return RecordCodecAccess::encode_failure(EncodeError::destination_size_mismatch);
    }
    if (const auto error = record_metadata_impl::validate_record_metadata(metadata, policy)) {
        return RecordCodecAccess::encode_failure(
            record_encoder_impl::encode_error_from_metadata(*error));
    }

    RecordHeader header{};
    header.format_bytes = prepared.format_size();
    header.args_bytes = prepared.args_size();
    header.arg_count = static_cast<std::uint16_t>(N);
    header.time_value = metadata.time_value;
    header.category_id = metadata.category_id;
    header.level = metadata.level;
    header.flags = metadata.flags;

    std::byte* cursor = destination + kRecordHeaderBytes;
    const auto format_size = prepared.format_size();
    const auto precomputed = prepared.precomputed_stored_hash();
    if (precomputed != 0U) {
        header.format_hash = precomputed;
        if (format_size != 0U) {
            std::memcpy(cursor, prepared.format_data(), format_size);
        }
    } else {
        const auto raw = format_size == 0U ? std::uint64_t{0}
                                           : hash_dispatch.copy_raw_unchecked(
                                                 prepared.format_data(), cursor, format_size);
        header.format_hash = stored_hash_from_raw(raw);
    }
    cursor += format_size;
    cursor = record_encoder_impl::encode_arguments_unchecked(cursor, prepared,
                                                             std::make_index_sequence<N>{});
    assert(cursor == destination + prepared.payload_size());
    std::memcpy(destination, &header, sizeof(header));
    return RecordCodecAccess::encode_success(prepared.payload_size());
}
}  // namespace qlog::detail