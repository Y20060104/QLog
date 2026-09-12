#include "qlog/detail/record_decoder.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <exception>
#include <type_traits>

#include "qlog/detail/argument_tag.hpp"
#include "qlog/detail/record_header.hpp"
#include "qlog/detail/record_limits.hpp"

namespace qlog::detail {
namespace {
template <class UInt>
[[nodiscard]] UInt load_le(const std::byte* source) noexcept {
    static_assert(std::is_same_v<UInt, std::uint8_t> || std::is_same_v<UInt, std::uint16_t> ||
                  std::is_same_v<UInt, std::uint32_t> || std::is_same_v<UInt, std::uint64_t>);
    UInt value{};
    std::memcpy(&value, source, sizeof(value));
    return value;
}

[[nodiscard]] DecodeFailure decode_failure_from_metadata(MetadataError error) noexcept {
    switch (error) {
        case MetadataError::invalid_level:
            return {DecodeError::invalid_level, 30U, 0xFFU};
        case MetadataError::unknown_flags:
            return {DecodeError::unknown_flags, 31U, 0xFFU};
        case MetadataError::reserved_timestamp_status:
            return {DecodeError::reserved_timestamp_status, 31U, 0xFFU};
        case MetadataError::invalid_time_value:
            return {DecodeError::invalid_time_value, 0U, 0xFFU};
        case MetadataError::fallback_timestamp_not_configured:
            return {DecodeError::fallback_timestamp_not_configured, 31U, 0xFFU};
    }
    assert(false);
    std::terminate();
}
}  // namespace

DecodeResult decode_v1(const std::byte* payload_data, std::size_t payload_size,
                       DecodedArg* workspace, std::size_t workspace_count,
                       const RecordValidationPolicy& policy) noexcept {
    const auto fail = [](DecodeError error, std::size_t offset,
                         std::uint8_t index = 0xFFU) noexcept {
        return RecordCodecAccess::decode_failure({error, offset, index});
    };
    if (payload_size != 0U && payload_data == nullptr)
        return fail(DecodeError::invalid_payload_metadata, 0U);
    if (workspace == nullptr || workspace_count != kMaxArgCount)
        return fail(DecodeError::invalid_workspace, 0U);
    if (payload_size < kRecordHeaderBytes) return fail(DecodeError::record_too_small, payload_size);

    RecordHeader header{};
    std::memcpy(&header, payload_data, sizeof(header));
    const RecordMetadata metadata{header.time_value, header.category_id, header.level,
                                  header.flags};
    if (const auto error = record_metadata_impl::validate_record_metadata(metadata, policy))
        return RecordCodecAccess::decode_failure(decode_failure_from_metadata(*error));
    if (header.format_bytes > kMaxFormatBytes) return fail(DecodeError::format_too_large, 16U);
    if (header.format_bytes > payload_size - kRecordHeaderBytes)
        return fail(DecodeError::invalid_format_length, 16U);
    const std::size_t args_begin = kRecordHeaderBytes + header.format_bytes;
    if (header.args_bytes != payload_size - args_begin)
        return fail(DecodeError::invalid_args_length, 20U);
    if (header.arg_count > kMaxArgCount) return fail(DecodeError::arg_count_exceeded, 28U);

    const std::size_t args_end = payload_size;
    std::size_t cursor = args_begin;
    for (std::size_t i = 0U; i < header.arg_count; ++i) {
        const auto index = static_cast<std::uint8_t>(i);
        if (cursor == args_end) return fail(DecodeError::decoded_count_mismatch, args_end, index);
        const std::size_t tag_offset = cursor;
        const auto tag =
            static_cast<ArgumentTag>(std::to_integer<std::uint8_t>(payload_data[cursor]));
        ++cursor;
        DecodedArg decoded{};
        decoded.tag = tag;
        switch (tag) {
            case ArgumentTag::Bool:
            case ArgumentTag::Char:
            case ArgumentTag::Int8:
            case ArgumentTag::UInt8:
                if (1U > args_end - cursor)
                    return fail(DecodeError::truncated_value, args_end, index);
                decoded.value.bits = load_le<std::uint8_t>(payload_data + cursor);
                if (tag == ArgumentTag::Bool && decoded.value.bits > 1U)
                    return fail(DecodeError::invalid_bool, cursor, index);
                cursor += 1U;
                break;
            case ArgumentTag::Int16:
            case ArgumentTag::UInt16:
                if (2U > args_end - cursor)
                    return fail(DecodeError::truncated_value, args_end, index);
                decoded.value.bits = load_le<std::uint16_t>(payload_data + cursor);
                cursor += 2U;
                break;
            case ArgumentTag::Int32:
            case ArgumentTag::UInt32:
            case ArgumentTag::F32:
                if (4U > args_end - cursor)
                    return fail(DecodeError::truncated_value, args_end, index);
                decoded.value.bits = load_le<std::uint32_t>(payload_data + cursor);
                cursor += 4U;
                break;
            case ArgumentTag::Int64:
            case ArgumentTag::UInt64:
            case ArgumentTag::F64:
            case ArgumentTag::Pointer64:
                if (8U > args_end - cursor)
                    return fail(DecodeError::truncated_value, args_end, index);
                decoded.value.bits = load_le<std::uint64_t>(payload_data + cursor);
                cursor += 8U;
                break;
            case ArgumentTag::Utf8String: {
                if (4U > args_end - cursor)
                    return fail(DecodeError::truncated_string_length, args_end, index);
                const auto size = load_le<std::uint32_t>(payload_data + cursor);
                cursor += 4U;
                if (size > args_end - cursor)
                    return fail(DecodeError::truncated_string, args_end, index);
                decoded.value.bytes = payload_data + cursor;
                decoded.byte_count = size;
                cursor += size;
                break;
            }
            case ArgumentTag::NullUtf8:
                break;
            case ArgumentTag::Invalid:
            default:
                return fail(DecodeError::invalid_or_unknown_tag, tag_offset, index);
        }
        workspace[i] = decoded;
    }
    if (cursor != args_end)
        return fail(DecodeError::trailing_args_bytes, cursor,
                    static_cast<std::uint8_t>(header.arg_count));
    return RecordCodecAccess::decode_success(header, payload_data + kRecordHeaderBytes, workspace);
}
}  // namespace qlog::detail
