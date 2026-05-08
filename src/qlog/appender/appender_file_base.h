#pragma once

#include "qlog/appender/appender_base.h"
#include "qlog/primitives/aligned_alloc.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace qlog::appender
{

class appender_file_base : public appender_base
{
protected:
    static constexpr size_t DEFAULT_BUFFER_ALIGNMENT = 64;

    struct parse_file_context
    {
        explicit parse_file_context(std::string file_name)
            : file_name_(std::move(file_name))
        {
        }

        void log_parse_fail_reason(const char* msg) const;
        std::string file_name_{};
    };

    struct write_with_cache_handle
    {
        uint8_t* data_ = nullptr;
        size_t alloc_len_ = 0;
        size_t used_len_ = 0;

        uint8_t* data() const
        {
            return data_;
        }
        size_t allocated_len() const
        {
            return alloc_len_;
        }
        void reset_used_len(size_t new_len)
        {
            used_len_ = new_len;
        }
    };

    struct read_with_cache_handle
    {
        const uint8_t* data_ = nullptr;
        size_t len_ = 0;

        const uint8_t* data() const
        {
            return data_;
        }
        size_t len() const
        {
            return len_;
        }
        read_with_cache_handle offset(size_t off) const
        {
            read_with_cache_handle out;
            if (off >= len_)
            {
                return out;
            }
            out.data_ = data_ + off;
            out.len_ = len_ - off;
            return out;
        }
    };

public:
    ~appender_file_base() override;

    virtual void flush_write_cache();
    void flush_write_io();

protected:
    bool init_impl(const appender_config& config) override;
    bool reset_impl(const appender_config& config) override;
    void log_impl(const entry_runtime_view& view) override;

    virtual bool parse_exist_log_file(parse_file_context& context) = 0;
    virtual std::string get_file_ext_name() = 0;
    virtual void on_file_open(bool is_new_created);

    virtual bool seek_read_file_absolute(size_t pos);
    virtual void seek_read_file_offset(int32_t offset);

    void on_log_item_recovery_begin(entry_runtime_view& read_view) override;
    void on_log_item_recovery_end() override;
    void on_log_item_new_begin(entry_runtime_view& read_view) override;

    [[nodiscard]] size_t get_current_file_size() const
    {
        return current_file_size_;
    }

    [[nodiscard]] const std::string& get_current_file_path() const
    {
        return current_file_path_;
    }

    virtual read_with_cache_handle read_with_cache(size_t size);
    void clear_read_cache();

    write_with_cache_handle alloc_write_cache(size_t size);
    void return_write_cache(const write_with_cache_handle& handle);

    size_t direct_write(const void* data, size_t size, int whence, int64_t seek_offset);
    void mark_write_finished();

    void set_cache_write_padding(uint8_t new_padding);

    virtual bool on_appender_file_recovery_begin();
    virtual void on_appender_file_recovery_end();

    void set_flush_when_destruct(bool flush)
    {
        flush_when_destruct_ = flush;
    }

    [[nodiscard]] size_t get_pending_flush_written_size() const
    {
        return cache_write_finished_cursor_;
    }
    [[nodiscard]] size_t get_written_size() const
    {
        return cache_write_cursor_;
    }
    [[nodiscard]] size_t get_cache_write_size() const
    {
        return cache_write_entity_.size() > cache_write_padding_
                   ? cache_write_entity_.size() - static_cast<size_t>(cache_write_padding_)
                   : 0;
    }
    [[nodiscard]] uint8_t* get_cache_write_ptr_base()
    {
        return cache_write_entity_.empty() ? nullptr
                                           : cache_write_entity_.data() + cache_write_padding_;
    }
    [[nodiscard]] bool is_read_of_cache_eof() const
    {
        return cache_read_eof_;
    }
    [[nodiscard]] size_t get_cache_write_head_size() const
    {
        return 0;
    }
    [[nodiscard]] size_t get_cache_read_cursor() const
    {
        return cache_read_cursor_;
    }
    [[nodiscard]] size_t get_read_file_pos() const
    {
        return read_file_pos_;
    }
    void flush() override
    {
    flush_write_cache();
    flush_write_io();
    }

private:
    void set_basic_configs(const appender_config& config);
    void refresh_file_handle(const entry_runtime_view& view);
    bool open_file_with_write_exclusive(const std::string& file_path);
    void resize_cache_write_entity(size_t new_size);
    void open_new_indexed_file_by_name();
    bool is_file_oversize() const;
    void clear_all_expired_files();
    void clear_all_limit_files();
    void clean_cache_write();

private:
    std::string config_file_name_{};
    bool always_create_new_file_ = false;
    uint64_t max_file_size_ = 0;
    uint64_t current_file_size_ = 0;
    std::FILE* file_ = nullptr;
    bool enable_rolling_log_file_ = true;
    uint64_t expire_time_ms_ = 0;
    uint64_t capacity_limit_ = 0;
    uint64_t current_file_expire_time_epoch_ms_ = 0;
    bool flush_when_destruct_ = true;
    std::string current_file_path_{};

    std::vector<uint8_t, qlog::aligned_allocator<uint8_t, DEFAULT_BUFFER_ALIGNMENT>>
        cache_write_entity_;
    size_t cache_write_cursor_ = 0;
    size_t cache_write_finished_cursor_ = 0;
    uint8_t cache_write_padding_ = 0;

    std::vector<uint8_t> cache_read_{};
    size_t cache_read_cursor_ = 0;
    size_t read_file_pos_ = 0;
    bool cache_read_eof_ = false;
};

} // namespace qlog::appender