#include "qlog/appender/appender_file_compressed.h"

#include "qlog/layout/utf_utils.h"
#include "qlog/serialization/serializer.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <string_view>

namespace qlog::appender
{

namespace
{
uint64_t fnv1a64(const uint8_t* p, size_t n)
{
    uint64_t h = 1469598103934665603ull;
    for (size_t i = 0; i < n; ++i)
    {
        h ^= static_cast<uint64_t>(p[i]);
        h *= 1099511628211ull;
    }
    return h;
}

uint64_t fnv1a64(std::string_view sv)
{
    return fnv1a64(reinterpret_cast<const uint8_t*>(sv.data()), sv.size());
}

uint64_t mix_format_hash(serialization::log_level level, uint32_t category_idx, uint64_t fmt_hash)
{
    const uint64_t mixer =
        (static_cast<uint64_t>(category_idx) << 32) | static_cast<uint64_t>(level);
    return fmt_hash ^ mixer;
}

template<typename U> U zigzag_encode(std::make_signed_t<U> v)
{
    using S = std::make_signed_t<U>;
    return static_cast<U>((v << 1) ^ (v >> (sizeof(S) * 8 - 1)));
}

template<typename U> std::make_signed_t<U> zigzag_decode(U v)
{
    using S = std::make_signed_t<U>;
    return static_cast<S>((v >> 1) ^ (static_cast<U>(0) - (v & 1)));
}

size_t vlq_encode_u64(uint64_t value, uint8_t* out)
{
    size_t n = 0;
    do
    {
        uint8_t b = static_cast<uint8_t>(value & 0x7F);
        value >>= 7;
        if (value)
        {
            b |= 0x80;
        }
        out[n++] = b;
    } while (value);
    return n;
}

size_t vlq_decode_u64(uint64_t& value, const uint8_t* in, size_t in_len)
{
    value = 0;
    uint64_t shift = 0;
    for (size_t i = 0; i < in_len && i < 10; ++i)
    {
        const uint8_t b = in[i];
        value |= static_cast<uint64_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0)
        {
            return i + 1;
        }
        shift += 7;
    }
    return 0;
}

void push_vlq_u64(std::vector<uint8_t>& out, uint64_t v)
{
    std::array<uint8_t, 10> tmp{};
    const auto n = vlq_encode_u64(v, tmp.data());
    out.insert(out.end(), tmp.begin(), tmp.begin() + static_cast<ptrdiff_t>(n));
}

bool read_vlq_u64(uint64_t& v, const uint8_t*& p, const uint8_t* end)
{
    const auto n = vlq_decode_u64(v, p, static_cast<size_t>(end - p));
    if (n == 0)
    {
        return false;
    }
    p += n;
    return true;
}
} // namespace

bool appender_file_compressed::init_impl(const appender_config& config)
{
    if (!appender_file_binary::init_impl(config))
    {
        return false;
    }
    format_templates_hash_cache_.reserve(4096);
    thread_info_hash_cache_.reserve(1024);
    return true;
}

void appender_file_compressed::on_file_open(bool is_new_created)
{
    appender_file_binary::on_file_open(is_new_created);
    if (is_new_created)
    {
        reset();
    }
}

std::string appender_file_compressed::get_file_ext_name()
{
    return ".logcompr";
}

uint32_t appender_file_compressed::get_binary_format_version() const
{
    return format_version;
}

void appender_file_compressed::reset()
{
    format_templates_hash_cache_.clear();
    thread_info_hash_cache_.clear();
    current_format_template_max_index_ = 0;
    current_thread_info_max_index_ = 0;
    last_log_entry_epoch_ = 0;
}

bool appender_file_compressed::parse_exist_log_file(parse_file_context& context)
{
    if (!appender_file_binary::parse_exist_log_file(context))
    {
        return false;
    }

    reset();
    while (true)
    {
        auto [ok, tp, data] = read_item_data(context);
        if (is_read_of_cache_eof())
        {
            return true;
        }
        if (!ok)
        {
            return false;
        }

        if (tp == item_type::log_template)
        {
            if (data.len() < 1)
            {
                context.log_parse_fail_reason("template data too short");
                return false;
            }
            const auto sub_type = static_cast<template_sub_type>(data.data()[0]);
            if (sub_type == template_sub_type::format_template_utf8 ||
                sub_type == template_sub_type::format_template_utf16)
            {
                if (!parse_format_template(context, data.offset(1), sub_type))
                {
                    return false;
                }
            }
            else if (sub_type == template_sub_type::thread_info_template)
            {
                if (!parse_thread_info_template(context, data.offset(1)))
                {
                    return false;
                }
            }
            else
            {
                context.log_parse_fail_reason("invalid template sub type");
                return false;
            }
        }
        else
        {
            if (!parse_log_entry(context, data))
            {
                return false;
            }
        }
    }
}

appender_file_compressed::read_item_result
appender_file_compressed::read_item_data(parse_file_context& context)
{
    auto h = read_with_cache(12);
    if (h.len() == 0 && is_read_of_cache_eof())
    {
        return {false, item_type::log_template, h};
    }
    if (h.len() < 2)
    {
        context.log_parse_fail_reason("read item head failed");
        return {false, item_type::log_template, h};
    }

    uint8_t first = h.data()[0];
    const int32_t offset = ((first & 0x7F) == 0) ? 1 : 0;
    const auto type = static_cast<item_type>(first & 0x80);

    uint64_t data_size_u64 = 0;
    const uint8_t* len_ptr = h.data() + offset;
    const auto len_size = vlq_decode_u64(data_size_u64, len_ptr, h.len() - offset);
    if (len_size == 0 || data_size_u64 > UINT32_MAX)
    {
        context.log_parse_fail_reason("decode item body len failed");
        return {false, type, h};
    }
    const uint32_t data_size = static_cast<uint32_t>(data_size_u64);

    seek_read_file_offset(
        static_cast<int32_t>(len_size + static_cast<size_t>(offset)) - static_cast<int32_t>(h.len())
    );

    h = read_with_cache(data_size);
    if (h.len() != data_size)
    {
        context.log_parse_fail_reason("read item body failed");
        return {false, type, h};
    }
    return {true, type, h};
}

bool appender_file_compressed::parse_log_entry(
    parse_file_context& context, const appender_file_base::read_with_cache_handle& data_handle
)
{
    if (data_handle.len() < 2)
    {
        context.log_parse_fail_reason("log entry body too short");
        return false;
    }
    const uint8_t* p = data_handle.data();
    const uint8_t* end = data_handle.data() + data_handle.len();

    uint64_t zz_epoch = 0;
    if (!read_vlq_u64(zz_epoch, p, end))
    {
        context.log_parse_fail_reason("log entry epoch decode failed");
        return false;
    }
    const int64_t diff = zigzag_decode<uint64_t>(zz_epoch);
    last_log_entry_epoch_ =
        static_cast<uint64_t>(static_cast<int64_t>(last_log_entry_epoch_) + diff);
    return true;
}

bool appender_file_compressed::parse_format_template(
    parse_file_context& context,
    const appender_file_base::read_with_cache_handle& data_handle,
    template_sub_type
)
{
    if (data_handle.len() < 2)
    {
        context.log_parse_fail_reason("format template too short");
        return false;
    }

    const uint8_t* p = data_handle.data();
    const uint8_t* end = data_handle.data() + data_handle.len();
    const auto level = static_cast<serialization::log_level>(*p++);

    uint64_t cat_u64 = 0;
    if (!read_vlq_u64(cat_u64, p, end))
    {
        context.log_parse_fail_reason("format template category decode failed");
        return false;
    }
    if (cat_u64 > UINT32_MAX)
    {
        context.log_parse_fail_reason("format template category overflow");
        return false;
    }

    const auto fmt_hash = fnv1a64(p, static_cast<size_t>(end - p));
    const auto h = mix_format_hash(level, static_cast<uint32_t>(cat_u64), fmt_hash);
    format_templates_hash_cache_[h] = current_format_template_max_index_++;
    return true;
}

bool appender_file_compressed::parse_thread_info_template(
    parse_file_context& context, const appender_file_base::read_with_cache_handle& data_handle
)
{
    const uint8_t* p = data_handle.data();
    const uint8_t* end = data_handle.data() + data_handle.len();
    if (p == end)
    {
        context.log_parse_fail_reason("thread template empty");
        return false;
    }

    uint64_t idx = 0;
    uint64_t tid = 0;
    if (!read_vlq_u64(idx, p, end) || !read_vlq_u64(tid, p, end))
    {
        context.log_parse_fail_reason("thread template decode failed");
        return false;
    }
    thread_info_hash_cache_[tid] = static_cast<uint32_t>(idx);
    current_thread_info_max_index_ =
        std::max(current_thread_info_max_index_, static_cast<uint32_t>(idx + 1));
    return true;
}

void appender_file_compressed::write_item(item_type type, const std::vector<uint8_t>& body)
{
    std::array<uint8_t, 10> len_buf{};
    const auto len_size = vlq_encode_u64(static_cast<uint64_t>(body.size()), len_buf.data());

    auto wh = alloc_write_cache(1 + len_size + body.size());
    wh.data()[0] = static_cast<uint8_t>(type); // 低7位恒0 -> 统一 offset=1 解码路径
    std::memcpy(wh.data() + 1, len_buf.data(), len_size);
    if (!body.empty())
    {
        std::memcpy(wh.data() + 1 + len_size, body.data(), body.size());
    }
    return_write_cache(wh);
}

void appender_file_compressed::log_impl(const entry_runtime_view& view)
{
    appender_file_binary::log_impl(view);

    const auto* hdr = view.header();
    if (!hdr || view.entry_size < sizeof(serialization::entry_header))
    {
        return;
    }

    const auto level = static_cast<serialization::log_level>(hdr->level);
    const auto fmt_hash = fnv1a64(view.format_string);
    const auto format_template_hash = mix_format_hash(level, hdr->category_idx, fmt_hash);

    uint32_t format_template_idx = UINT32_MAX;
    auto fit = format_templates_hash_cache_.find(format_template_hash);
    if (fit == format_templates_hash_cache_.end())
    {
        std::vector<uint8_t> body;
        body.reserve(view.format_string.size() + 16);
        body.push_back(static_cast<uint8_t>(template_sub_type::format_template_utf8));
        body.push_back(static_cast<uint8_t>(level));
        push_vlq_u64(body, hdr->category_idx);
        body.insert(
            body.end(),
            reinterpret_cast<const uint8_t*>(view.format_string.data()),
            reinterpret_cast<const uint8_t*>(view.format_string.data() + view.format_string.size())
        );
        write_item(item_type::log_template, body);
        format_template_idx = current_format_template_max_index_;
        format_templates_hash_cache_[format_template_hash] = current_format_template_max_index_++;
    }
    else
    {
        format_template_idx = fit->second;
    }

    uint32_t thread_template_idx = UINT32_MAX;
    auto tit = thread_info_hash_cache_.find(hdr->thread_id);
    if (tit == thread_info_hash_cache_.end())
    {
        std::vector<uint8_t> body;
        body.reserve(24);
        body.push_back(static_cast<uint8_t>(template_sub_type::thread_info_template));
        push_vlq_u64(body, current_thread_info_max_index_);
        push_vlq_u64(body, hdr->thread_id);
        write_item(item_type::log_template, body);
        thread_template_idx = current_thread_info_max_index_;
        thread_info_hash_cache_[hdr->thread_id] = current_thread_info_max_index_++;
    }
    else
    {
        thread_template_idx = tit->second;
    }

    const uint8_t* args = view.entry_data + sizeof(serialization::entry_header);
    const uint8_t* args_end = view.entry_data + view.entry_size;

    std::vector<uint8_t> body;
    body.reserve(view.entry_size * 2);

    const int64_t epoch_delta =
        static_cast<int64_t>(hdr->timestamp_ms) - static_cast<int64_t>(last_log_entry_epoch_);
    last_log_entry_epoch_ = hdr->timestamp_ms;
    push_vlq_u64(body, zigzag_encode<uint64_t>(epoch_delta));
    push_vlq_u64(body, format_template_idx);
    push_vlq_u64(body, thread_template_idx);

    while (args < args_end)
    {
        const auto type = static_cast<serialization::param_type>(*args++);
        switch (type)
        {
        case serialization::param_type::type_null:
            body.push_back(static_cast<uint8_t>(type));
            break;

        case serialization::param_type::type_bool:
        case serialization::param_type::type_int8:
        case serialization::param_type::type_uint8:
        {
            if (args_end - args < 1)
                return;
            body.push_back(static_cast<uint8_t>(type));
            body.push_back(*args++);
            break;
        }

        case serialization::param_type::type_int16:
        {
            if (args_end - args < 2)
                return;
            int16_t v = 0;
            std::memcpy(&v, args, sizeof(v));
            args += sizeof(v);
            body.push_back(static_cast<uint8_t>(type));
            push_vlq_u64(body, zigzag_encode<uint16_t>(v));
            break;
        }
        case serialization::param_type::type_uint16:
        {
            if (args_end - args < 2)
                return;
            uint16_t v = 0;
            std::memcpy(&v, args, sizeof(v));
            args += sizeof(v);
            body.push_back(static_cast<uint8_t>(type));
            push_vlq_u64(body, v);
            break;
        }
        case serialization::param_type::type_int32:
        {
            if (args_end - args < 4)
                return;
            int32_t v = 0;
            std::memcpy(&v, args, sizeof(v));
            args += sizeof(v);
            body.push_back(static_cast<uint8_t>(type));
            push_vlq_u64(body, zigzag_encode<uint32_t>(v));
            break;
        }
        case serialization::param_type::type_uint32:
        {
            if (args_end - args < 4)
                return;
            uint32_t v = 0;
            std::memcpy(&v, args, sizeof(v));
            args += sizeof(v);
            body.push_back(static_cast<uint8_t>(type));
            push_vlq_u64(body, v);
            break;
        }
        case serialization::param_type::type_int64:
        {
            if (args_end - args < 8)
                return;
            int64_t v = 0;
            std::memcpy(&v, args, sizeof(v));
            args += sizeof(v);
            body.push_back(static_cast<uint8_t>(type));
            push_vlq_u64(body, zigzag_encode<uint64_t>(v));
            break;
        }
        case serialization::param_type::type_uint64:
        {
            if (args_end - args < 8)
                return;
            uint64_t v = 0;
            std::memcpy(&v, args, sizeof(v));
            args += sizeof(v);
            body.push_back(static_cast<uint8_t>(type));
            push_vlq_u64(body, v);
            break;
        }

        case serialization::param_type::type_pointer:
        {
            if (args_end - args < static_cast<ptrdiff_t>(sizeof(uintptr_t)))
                return;
            uintptr_t v = 0;
            std::memcpy(&v, args, sizeof(v));
            args += sizeof(v);
            body.push_back(static_cast<uint8_t>(type));
            std::array<uint8_t, sizeof(uintptr_t)> raw{};
            std::memcpy(raw.data(), &v, sizeof(v));
            body.insert(body.end(), raw.begin(), raw.end());
            break;
        }

        case serialization::param_type::type_float:
        {
            if (args_end - args < 4)
                return;
            body.push_back(static_cast<uint8_t>(type));
            body.insert(body.end(), args, args + 4);
            args += 4;
            break;
        }
        case serialization::param_type::type_double:
        {
            if (args_end - args < 8)
                return;
            body.push_back(static_cast<uint8_t>(type));
            body.insert(body.end(), args, args + 8);
            args += 8;
            break;
        }

        case serialization::param_type::type_string_utf8:
        {
            if (args_end - args < 4)
                return;
            uint32_t len = 0;
            std::memcpy(&len, args, 4);
            args += 4;
            if (args_end - args < static_cast<ptrdiff_t>(len))
                return;
            body.push_back(static_cast<uint8_t>(type));
            push_vlq_u64(body, len);
            body.insert(body.end(), args, args + static_cast<ptrdiff_t>(len));
            args += len;
            break;
        }

        case serialization::param_type::type_string_utf16:
        {
            if (args_end - args < 4)
                return;
            uint32_t byte_len = 0;
            std::memcpy(&byte_len, args, 4);
            args += 4;
            if (args_end - args < static_cast<ptrdiff_t>(byte_len))
                return;
            std::vector<uint8_t> utf8(byte_len * 2 + 4);
            const auto out_len = layout::utf::utf16le_to_utf8(args, byte_len, utf8.data());
            body.push_back(static_cast<uint8_t>(serialization::param_type::type_string_utf8));
            push_vlq_u64(body, out_len);
            body.insert(body.end(), utf8.begin(), utf8.begin() + static_cast<ptrdiff_t>(out_len));
            args += byte_len;
            break;
        }

        case serialization::param_type::type_string_utf32:
        {
            if (args_end - args < 4)
                return;
            uint32_t byte_len = 0;
            std::memcpy(&byte_len, args, 4);
            args += 4;
            if (args_end - args < static_cast<ptrdiff_t>(byte_len))
                return;
            std::vector<uint8_t> utf8(byte_len + 4);
            const auto out_len = layout::utf::utf32le_to_utf8(args, byte_len, utf8.data());
            body.push_back(static_cast<uint8_t>(serialization::param_type::type_string_utf8));
            push_vlq_u64(body, out_len);
            body.insert(body.end(), utf8.begin(), utf8.begin() + static_cast<ptrdiff_t>(out_len));
            args += byte_len;
            break;
        }
        }
    }

    write_item(item_type::log_entry, body);
    mark_write_finished();
}

} // namespace qlog::appender