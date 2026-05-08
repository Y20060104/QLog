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
    static_assert(
        sizeof(appender_file_segment_head) == 12, "appender_file_segment_head size error"
    );

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
        uint8_t* data, size_t size, const uint8_t* key, size_t key_size, uint64_t stream_offset
    );

private:
    seg_info cur_read_seg_{};
    appender_encryption_type enc_type_ = appender_encryption_type::plaintext;
    std::vector<
        uint8_t,
        qlog::aligned_allocator<uint8_t, appender_file_base::DEFAULT_BUFFER_ALIGNMENT>>
        xor_key_blob_;
};

} // namespace qlog::appender