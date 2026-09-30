#include "qlog/record/log_entry_handle.hpp"

#include <cstring>

#include "qlog/record/argument_tag.hpp"
namespace qlog::record {
bool LogEntryHandle::validate() const {
    if (!data_ptr_ || data_len_ < sizeof(RecordHeader) + sizeof(RecordExtHeader)) {
        return false;
    }
    RecordHeader header;
    std::memcpy(&header, data_ptr_, sizeof(RecordHeader));
    const auto fmt = static_cast<ArgumentTag>(header.log_format_str_type);
    if (fmt != ArgumentTag::string_utf8_type && fmt != ArgumentTag::string_utf16_type &&
        fmt != ArgumentTag::string_utf32_type) {
        return false;
    }

    const std::uint64_t args_begin =
        sizeof(RecordHeader) +
        ((std::uint64_t{header.log_format_data_len} + 3U) & ~std::uint64_t{3});
    const std::uint64_t ext = header.ext_info_offset;
    if (args_begin > data_len_ || ext < args_begin || ext + 1U > data_len_) {
        return false;
    }  // The one-byte extension header must fit in the record.
    if (ext + sizeof(RecordExtHeader) + data_ptr_[static_cast<std::size_t>(ext)] > data_len_) {
        return false;
    }

    std::uint64_t cursor = args_begin;
    while (cursor < ext) {
        const auto remaining = ext - cursor;
        if (remaining < 4U) return false;
        const auto* p = data_ptr_ + static_cast<std::size_t>(cursor);
        const auto tag = static_cast<ArgumentTag>(*p);
        std::uint64_t step;
        switch (tag) {
            case ArgumentTag::null_type:
            case ArgumentTag::bool_type:
            case ArgumentTag::char_type:
            case ArgumentTag::char16_type:
            case ArgumentTag::int8_type:
            case ArgumentTag::uint8_type:
            case ArgumentTag::int16_type:
            case ArgumentTag::uint16_type:
                step = 4;
                break;
            case ArgumentTag::char32_type:
            case ArgumentTag::int32_type:
            case ArgumentTag::uint32_type:
            case ArgumentTag::float_type:
                step = 8;
                break;
            case ArgumentTag::pointer_type:
            case ArgumentTag::int64_type:
            case ArgumentTag::uint64_type:
            case ArgumentTag::double_type:
                step = 12;
                break;
            case ArgumentTag::string_utf8_type:
            case ArgumentTag::string_utf16_type: {
                if (remaining < 8U) return false;
                std::uint32_t n;
                std::memcpy(&n, p + 4, sizeof(n));
                step = 8U + ((std::uint64_t{n} + 3U) & ~std::uint64_t{3});
                break;
            }
            default:
                return false;
        }
        if (step > remaining) return false;
        cursor += step;
    }
    return cursor == ext;
}

}  // namespace  qlog::record
