# M6 `appender_file_binary.h` / `appender_file_binary.cpp`

## `src/qlog/appender/appender_file_binary.h`

```cpp
#pragma once

#include "qlog/appender/appender_file_base.h"

#include <array>
#include <cstdint>
#include <vector>

namespace qlog::appender
{

class appender_file_binary : public appender_file_base
{
public:
    enum class appender_format_type : uint8_t
    {
        raw = 1,
        compressed = 2
    };

    enum class appender_encryption_type : uint8_t
    {
        plaintext = 1,
        rsa_aes_xor = 2
    };

    enum class appender_segment_type : uint8_t
    {
        normal = 1,
        recovery_by_appender = 2,
        recovery_by_log_buffer = 3
    };

#pragma pack(push, 1)
    struct appender_file_header
    {
        uint32_t version;
        appender_format_type format;
        char padding[3];
    };

    struct appender_file_segment_head
    {
        uint64_t next_seg_pos;
        appender_segment_type seg_type;
        appender_encryption_type enc_type;
        bool has_key;
        char padding[1];
    };

    struct appender_payload_metadata
    {
        char magic_number[3];
        bool use_local_time;
        int32_t gmt_offset_hours;
        int32_t gmt_offset_minutes;
        int32_t time_zone_diff_to_gmt_ms;
        char time_zone_str[32];
        uint32_t category_count;
    };
#pragma pack(pop)

    static_assert(sizeof(appender_file_header) == 8, "appender_file_header size error");
    static_assert(sizeof(appender_file_segment_head) == 12, "appender_file_segment_head size error");

    struct seg_info
    {
        uint64_t start_pos = 0;
        uint64_t end_pos = 0;
        appender_encryption_type enc_type = appender_encryption_type::plaintext;
    };

protected:
    static constexpr size_t get_xor_key_blob_size()
    {
        return 32 * 1024;
    }

    static constexpr size_t get_encryption_keys_size()
    {
        return 256 + 16 + get_xor_key_blob_size();
    }

protected:
    bool init_impl(const appender_config& config) override;
    bool reset_impl(const appender_config& config) override;
    bool parse_exist_log_file(parse_file_context& context) override;
    void on_file_open(bool is_new_created) override;
    void flush_write_cache() override;
    bool seek_read_file_absolute(size_t pos) override;
    void seek_read_file_offset(int32_t offset) override;

    virtual appender_format_type get_appender_format() const = 0;
    virtual uint32_t get_binary_format_version() const = 0;

    bool on_appender_file_recovery_begin() override;
    void on_appender_file_recovery_end() override;
    void on_log_item_recovery_begin(entry_runtime_view& read_view) override;
    void on_log_item_recovery_end() override;
    void on_log_item_new_begin(entry_runtime_view& read_view) override;
    read_with_cache_handle read_with_cache(size_t size) override;

private:
    bool read_to_correct_segment();
    bool read_to_next_segment();
    void append_new_segment(appender_segment_type type);
    void update_write_cache_padding();
    static void xor_crypt(
        uint8_t* data,
        size_t size,
        const uint8_t* key,
        size_t key_size,
        uint64_t stream_offset
    );

private:
    seg_info cur_read_seg_ {};
    appender_encryption_type enc_type_ = appender_encryption_type::plaintext;
    std::vector<uint8_t, qlog::aligned_allocator<uint8_t, appender_file_base::DEFAULT_BUFFER_ALIGNMENT>>
        xor_key_blob_ {};
};

} // namespace qlog::appender
```

## `src/qlog/appender/appender_file_binary.cpp`

```cpp
#include "qlog/appender/appender_file_binary.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <random>

namespace qlog::appender
{

bool appender_file_binary::init_impl(const appender_config& config)
{
    enc_type_ = config.public_key.empty() ? appender_encryption_type::plaintext :
                                            appender_encryption_type::rsa_aes_xor;
    xor_key_blob_.clear();
    return appender_file_base::init_impl(config);
}

bool appender_file_binary::reset_impl(const appender_config& config)
{
    if (!appender_file_base::reset_impl(config))
    {
        return false;
    }
    const auto prev_type = enc_type_;
    const auto new_type = config.public_key.empty() ? appender_encryption_type::plaintext :
                                                      appender_encryption_type::rsa_aes_xor;
    return prev_type == new_type;
}

bool appender_file_binary::parse_exist_log_file(parse_file_context& context)
{
    seek_read_file_absolute(0);

    auto rh = appender_file_base::read_with_cache(sizeof(appender_file_header));
    if (rh.len() < sizeof(appender_file_header))
    {
        context.log_parse_fail_reason("read appender_file_header failed");
        return false;
    }

    appender_file_header file_head {};
    std::memcpy(&file_head, rh.data(), sizeof(file_head));
    if (file_head.version != get_binary_format_version())
    {
        context.log_parse_fail_reason("format version incompatible");
        return false;
    }
    if (file_head.format != get_appender_format())
    {
        context.log_parse_fail_reason("format incompatible");
        return false;
    }

    cur_read_seg_.end_pos = sizeof(appender_file_header);
    if (!read_to_next_segment())
    {
        context.log_parse_fail_reason("parse first appender_file_segment_head failed");
        return false;
    }
    if (cur_read_seg_.enc_type != appender_encryption_type::plaintext &&
        cur_read_seg_.enc_type != appender_encryption_type::rsa_aes_xor)
    {
        context.log_parse_fail_reason("invalid enc type");
        return false;
    }

    rh = read_with_cache(sizeof(appender_payload_metadata));
    if (rh.len() < sizeof(appender_payload_metadata))
    {
        context.log_parse_fail_reason("read appender_payload_metadata failed");
        return false;
    }

    appender_payload_metadata md {};
    std::memcpy(&md, rh.data(), sizeof(md));
    if (md.magic_number[0] != 2 || md.magic_number[1] != 2 || md.magic_number[2] != 7)
    {
        context.log_parse_fail_reason("metadata magic mismatch");
        return false;
    }

    const uint32_t category_count = md.category_count;
    if (!categories_name_ || category_count != categories_name_->size())
    {
        context.log_parse_fail_reason("category count mismatch");
        return false;
    }

    for (uint32_t i = 0; i < category_count; ++i)
    {
        rh = read_with_cache(sizeof(uint32_t));
        if (rh.len() < sizeof(uint32_t))
        {
            context.log_parse_fail_reason("read category name length failed");
            return false;
        }
        uint32_t name_len = 0;
        std::memcpy(&name_len, rh.data(), sizeof(name_len));
        rh = read_with_cache(name_len);
        if (rh.len() < name_len)
        {
            context.log_parse_fail_reason("read category name content failed");
            return false;
        }
        if ((*categories_name_)[i].size() != name_len ||
            std::memcmp((*categories_name_)[i].data(), rh.data(), name_len) != 0)
        {
            context.log_parse_fail_reason("category name mismatch");
            return false;
        }
    }
    return true;
}

void appender_file_binary::on_file_open(bool is_new_created)
{
    appender_file_base::on_file_open(is_new_created);

    if (enc_type_ == appender_encryption_type::rsa_aes_xor)
    {
        xor_key_blob_.clear();
    }

    cur_read_seg_.start_pos = sizeof(appender_file_header);
    cur_read_seg_.end_pos = get_current_file_size();

    if (!is_new_created)
    {
        return;
    }

    appender_file_header file_head {};
    file_head.format = get_appender_format();
    file_head.version = get_binary_format_version();
    direct_write(&file_head, sizeof(file_head), SEEK_END, 0);

    append_new_segment(appender_segment_type::normal);

    appender_payload_metadata md {};
    md.magic_number[0] = 2;
    md.magic_number[1] = 2;
    md.magic_number[2] = 7;
    md.use_local_time = use_local_time_;
    md.gmt_offset_hours = gmt_offset_minutes_ / 60;
    md.gmt_offset_minutes = gmt_offset_minutes_ % 60;
    md.time_zone_diff_to_gmt_ms = gmt_offset_minutes_ * 60 * 1000;
    std::snprintf(md.time_zone_str, sizeof(md.time_zone_str), "GMT%+d", md.gmt_offset_hours);
    md.category_count = categories_name_ ? static_cast<uint32_t>(categories_name_->size()) : 0;

    auto wh = alloc_write_cache(sizeof(md));
    std::memcpy(wh.data(), &md, sizeof(md));
    return_write_cache(wh);

    if (categories_name_)
    {
        for (const auto& name : *categories_name_)
        {
            const uint32_t n = static_cast<uint32_t>(name.size());
            wh = alloc_write_cache(sizeof(uint32_t) + n);
            std::memcpy(wh.data(), &n, sizeof(uint32_t));
            std::memcpy(wh.data() + sizeof(uint32_t), name.data(), n);
            return_write_cache(wh);
        }
    }
    mark_write_finished();
    flush_write_cache();
}

void appender_file_binary::xor_crypt(
    uint8_t* data,
    size_t size,
    const uint8_t* key,
    size_t key_size,
    uint64_t stream_offset
)
{
    if (!data || !key || key_size == 0)
    {
        return;
    }
    for (size_t i = 0; i < size; ++i)
    {
        const size_t ki = static_cast<size_t>((stream_offset + i) & (key_size - 1));
        data[i] ^= key[ki];
    }
}

void appender_file_binary::flush_write_cache()
{
    if (xor_key_blob_.empty() || get_pending_flush_written_size() == 0)
    {
        appender_file_base::flush_write_cache();
        return;
    }

    auto* p = get_cache_write_ptr_base();
    const auto sz = get_pending_flush_written_size();
    xor_crypt(p, sz, xor_key_blob_.data(), xor_key_blob_.size(), get_current_file_size());
    appender_file_base::flush_write_cache();
    update_write_cache_padding();
    if (get_pending_flush_written_size() > 0)
    {
        xor_crypt(
            get_cache_write_ptr_base(),
            get_pending_flush_written_size(),
            xor_key_blob_.data(),
            xor_key_blob_.size(),
            get_current_file_size()
        );
    }
}

bool appender_file_binary::seek_read_file_absolute(size_t pos)
{
    if (!appender_file_base::seek_read_file_absolute(pos))
    {
        return false;
    }
    return true;
}

void appender_file_binary::seek_read_file_offset(int32_t offset)
{
    appender_file_base::seek_read_file_offset(offset);
    if (cur_read_seg_.start_pos > get_read_file_pos() || cur_read_seg_.end_pos < get_read_file_pos())
    {
        read_to_correct_segment();
    }
}

bool appender_file_binary::on_appender_file_recovery_begin()
{
    if (!appender_file_base::on_appender_file_recovery_begin())
    {
        return false;
    }
    append_new_segment(appender_segment_type::recovery_by_appender);
    return true;
}

void appender_file_binary::on_appender_file_recovery_end()
{
    appender_file_base::on_appender_file_recovery_end();
}

void appender_file_binary::on_log_item_recovery_begin(entry_runtime_view& read_view)
{
    appender_file_base::on_log_item_recovery_begin(read_view);
    append_new_segment(appender_segment_type::recovery_by_log_buffer);
}

void appender_file_binary::on_log_item_recovery_end()
{
    appender_file_base::on_log_item_recovery_end();
}

void appender_file_binary::on_log_item_new_begin(entry_runtime_view& read_view)
{
    appender_file_base::on_log_item_new_begin(read_view);
    append_new_segment(appender_segment_type::normal);
}

appender_file_base::read_with_cache_handle appender_file_binary::read_with_cache(size_t size)
{
    auto current = static_cast<uint64_t>(get_read_file_pos());
    while (current == cur_read_seg_.end_pos)
    {
        if (!read_to_next_segment())
        {
            return appender_file_base::read_with_cache(0);
        }
        current = static_cast<uint64_t>(get_read_file_pos());
    }

    if (current + size > cur_read_seg_.end_pos)
    {
        size = static_cast<size_t>(cur_read_seg_.end_pos - current);
    }
    return appender_file_base::read_with_cache(size);
}

bool appender_file_binary::read_to_correct_segment()
{
    const auto backup = get_read_file_pos();
    cur_read_seg_.end_pos = sizeof(appender_file_header);
    while (read_to_next_segment())
    {
        if (backup >= cur_read_seg_.start_pos && backup <= cur_read_seg_.end_pos)
        {
            seek_read_file_absolute(backup);
            return true;
        }
    }
    return false;
}

bool appender_file_binary::read_to_next_segment()
{
    const auto new_seg_start_pos = cur_read_seg_.end_pos;
    if (new_seg_start_pos == UINT64_MAX)
    {
        return false;
    }
    if (!seek_read_file_absolute(static_cast<size_t>(new_seg_start_pos)))
    {
        return false;
    }

    auto rh = appender_file_base::read_with_cache(sizeof(appender_file_segment_head));
    if (rh.len() < sizeof(appender_file_segment_head))
    {
        return false;
    }

    appender_file_segment_head seg_head {};
    std::memcpy(&seg_head, rh.data(), sizeof(seg_head));
    if (seg_head.next_seg_pos < new_seg_start_pos + sizeof(appender_file_segment_head))
    {
        return false;
    }

    cur_read_seg_.start_pos = new_seg_start_pos;
    cur_read_seg_.end_pos = seg_head.next_seg_pos;
    cur_read_seg_.enc_type = seg_head.enc_type;

    if (seg_head.has_key)
    {
        auto key_handle = appender_file_base::read_with_cache(get_encryption_keys_size());
        if (key_handle.len() < get_encryption_keys_size())
        {
            return false;
        }
        xor_key_blob_.resize(get_xor_key_blob_size());
        std::memcpy(
            xor_key_blob_.data(),
            key_handle.data() + 256 + 16,
            get_xor_key_blob_size()
        );
    }
    return true;
}

void appender_file_binary::append_new_segment(appender_segment_type type)
{
    clear_read_cache();
    cur_read_seg_.end_pos = sizeof(appender_file_header);

    bool has_segment = false;
    while (read_to_next_segment())
    {
        has_segment = true;
    }

    if (has_segment)
    {
        const uint64_t new_seg_start_pos = get_current_file_size();
        direct_write(
            &new_seg_start_pos,
            sizeof(new_seg_start_pos),
            SEEK_SET,
            static_cast<int64_t>(cur_read_seg_.start_pos)
        );
    }

    appender_file_segment_head seg {};
    seg.next_seg_pos = UINT64_MAX;
    seg.seg_type = type;
    seg.enc_type = enc_type_;
    seg.has_key = (enc_type_ == appender_encryption_type::rsa_aes_xor) && xor_key_blob_.empty();
    direct_write(&seg, sizeof(seg), SEEK_END, 0);

    if (seg.has_key)
    {
        std::array<uint8_t, 256> fake_rsa_cipher {};
        std::array<uint8_t, 16> fake_iv {};
        xor_key_blob_.resize(get_xor_key_blob_size());

        std::random_device rd;
        std::mt19937_64 gen(rd());
        for (auto& v : xor_key_blob_)
        {
            v = static_cast<uint8_t>(gen() & 0xFF);
        }
        for (auto& v : fake_rsa_cipher)
        {
            v = static_cast<uint8_t>(gen() & 0xFF);
        }
        for (auto& v : fake_iv)
        {
            v = static_cast<uint8_t>(gen() & 0xFF);
        }

        direct_write(fake_rsa_cipher.data(), fake_rsa_cipher.size(), SEEK_CUR, 0);
        direct_write(fake_iv.data(), fake_iv.size(), SEEK_CUR, 0);
        direct_write(xor_key_blob_.data(), xor_key_blob_.size(), SEEK_CUR, 0);
    }

    flush_write_io();
    update_write_cache_padding();
}

void appender_file_binary::update_write_cache_padding()
{
    if (enc_type_ != appender_encryption_type::rsa_aes_xor || xor_key_blob_.empty())
    {
        set_cache_write_padding(0);
        return;
    }

    const auto file_pos = get_current_file_size();
    const size_t enc_start_pos = static_cast<size_t>(file_pos & (xor_key_blob_.size() - 1));
    const size_t target_align = enc_start_pos & (DEFAULT_BUFFER_ALIGNMENT - 1);
    const size_t current_align = get_cache_write_head_size() & (DEFAULT_BUFFER_ALIGNMENT - 1);
    const uint8_t target_padding = static_cast<uint8_t>(
        (target_align + DEFAULT_BUFFER_ALIGNMENT - current_align) & (DEFAULT_BUFFER_ALIGNMENT - 1)
    );
    set_cache_write_padding(target_padding);
}

} // namespace qlog::appender
```
