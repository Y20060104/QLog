# M6 `appender_file_base.h` / `appender_file_base.cpp`

## `src/qlog/appender/appender_file_base.h`

```cpp
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
        std::string file_name_ {};
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
        return cache_write_entity_.size() > cache_write_padding_ ?
                   cache_write_entity_.size() - static_cast<size_t>(cache_write_padding_) :
                   0;
    }
    [[nodiscard]] uint8_t* get_cache_write_ptr_base()
    {
        return cache_write_entity_.empty() ? nullptr : cache_write_entity_.data() + cache_write_padding_;
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
    std::string config_file_name_ {};
    bool always_create_new_file_ = false;
    uint64_t max_file_size_ = 0;
    uint64_t current_file_size_ = 0;
    std::FILE* file_ = nullptr;
    bool enable_rolling_log_file_ = true;
    uint64_t expire_time_ms_ = 0;
    uint64_t capacity_limit_ = 0;
    uint64_t current_file_expire_time_epoch_ms_ = 0;
    bool flush_when_destruct_ = true;
    std::string current_file_path_ {};

    std::vector<uint8_t, qlog::aligned_allocator<uint8_t, DEFAULT_BUFFER_ALIGNMENT>>
        cache_write_entity_ {};
    size_t cache_write_cursor_ = 0;
    size_t cache_write_finished_cursor_ = 0;
    uint8_t cache_write_padding_ = 0;

    std::vector<uint8_t> cache_read_ {};
    size_t cache_read_cursor_ = 0;
    size_t read_file_pos_ = 0;
    bool cache_read_eof_ = false;
};

} // namespace qlog::appender
```

## `src/qlog/appender/appender_file_base.cpp`

```cpp
#include "qlog/appender/appender_file_base.h"

#include "qlog/serialization/entry_format.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <string_view>

namespace qlog::appender
{

namespace
{
constexpr size_t k_cache_read_default_size = 32 * 1024;
constexpr size_t k_cache_write_default_size = 64 * 1024;

uint64_t now_epoch_ms()
{
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        )
            .count()
    );
}

uint64_t get_entry_epoch_ms(const entry_runtime_view& view)
{
    const auto* hdr = view.header();
    return hdr ? hdr->timestamp_ms : now_epoch_ms();
}
} // namespace

appender_file_base::~appender_file_base()
{
    if (flush_when_destruct_)
    {
        flush_write_cache();
        flush_write_io();
    }
    if (file_)
    {
        std::fclose(file_);
        file_ = nullptr;
    }
}

void appender_file_base::flush_write_cache()
{
    if (!file_ || cache_write_finished_cursor_ == 0)
    {
        return;
    }

    auto* base = get_cache_write_ptr_base();
    const size_t need_write_size = cache_write_finished_cursor_;
    const size_t real_write_size = std::fwrite(base, 1, need_write_size, file_);

    if (real_write_size < need_write_size)
    {
        std::memmove(base, base + static_cast<ptrdiff_t>(real_write_size), need_write_size - real_write_size);
    }

    if (cache_write_cursor_ > need_write_size)
    {
        std::memmove(
            base + static_cast<ptrdiff_t>(need_write_size - real_write_size),
            base + static_cast<ptrdiff_t>(need_write_size),
            cache_write_cursor_ - need_write_size
        );
    }

    cache_write_finished_cursor_ -= real_write_size;
    cache_write_cursor_ -= real_write_size;
    current_file_size_ += real_write_size;

    if (cache_write_entity_.size() > k_cache_write_default_size &&
        (cache_write_cursor_ + cache_write_padding_) <= (k_cache_read_default_size >> 1))
    {
        resize_cache_write_entity(k_cache_write_default_size);
    }
}

void appender_file_base::flush_write_io()
{
    if (!file_)
    {
        return;
    }
    std::fflush(file_);
}

bool appender_file_base::init_impl(const appender_config& config)
{
    set_basic_configs(config);
    resize_cache_write_entity(k_cache_write_default_size);
    open_new_indexed_file_by_name();
    return !config_file_name_.empty() && file_ != nullptr;
}

bool appender_file_base::reset_impl(const appender_config& config)
{
    const auto prev_name = config_file_name_;
    set_basic_configs(config);
    return prev_name == config_file_name_;
}

void appender_file_base::log_impl(const entry_runtime_view& view)
{
    refresh_file_handle(view);
}

void appender_file_base::on_file_open(bool)
{
}

bool appender_file_base::seek_read_file_absolute(size_t pos)
{
    if (!file_)
    {
        return false;
    }
    if (std::fseek(file_, static_cast<long>(pos), SEEK_SET) != 0)
    {
        return false;
    }
    read_file_pos_ = pos;
    clear_read_cache();
    cache_read_eof_ = false;
    return true;
}

void appender_file_base::seek_read_file_offset(int32_t offset)
{
    const auto final_cursor = static_cast<int64_t>(cache_read_cursor_) + offset;
    if (final_cursor >= 0 && final_cursor <= static_cast<int64_t>(cache_read_.size()))
    {
        cache_read_cursor_ = static_cast<size_t>(final_cursor);
        read_file_pos_ = static_cast<size_t>(static_cast<int64_t>(read_file_pos_) + offset);
    }
}

void appender_file_base::on_log_item_recovery_begin(entry_runtime_view& read_view)
{
    flush_write_cache();
    refresh_file_handle(read_view);
}

void appender_file_base::on_log_item_recovery_end()
{
}

void appender_file_base::on_log_item_new_begin(entry_runtime_view& read_view)
{
    flush_write_cache();
    refresh_file_handle(read_view);
}

appender_file_base::read_with_cache_handle appender_file_base::read_with_cache(size_t size)
{
    read_with_cache_handle out;
    if (!file_)
    {
        return out;
    }

    const auto left = cache_read_.size() - cache_read_cursor_;
    if (left < size)
    {
        cache_read_.erase(cache_read_.begin(), cache_read_.begin() + static_cast<ptrdiff_t>(cache_read_cursor_));
        const auto total_size = std::max(size, k_cache_read_default_size);
        const auto fill_size = total_size - left;
        const auto old_size = cache_read_.size();
        cache_read_.resize(old_size + fill_size);

        const auto got = std::fread(cache_read_.data() + old_size, 1, fill_size, file_);
        cache_read_.resize(old_size + got);
        cache_read_cursor_ = 0;
        if (got < fill_size)
        {
            cache_read_eof_ = true;
        }
    }

    out.data_ = cache_read_.data() + static_cast<ptrdiff_t>(cache_read_cursor_);
    out.len_ = std::min(size, cache_read_.size() - cache_read_cursor_);
    cache_read_cursor_ += out.len_;
    read_file_pos_ += out.len_;
    return out;
}

void appender_file_base::clear_read_cache()
{
    cache_read_cursor_ = 0;
    cache_read_.clear();
    if (cache_read_.capacity() > k_cache_read_default_size)
    {
        cache_read_.shrink_to_fit();
    }
}

appender_file_base::write_with_cache_handle appender_file_base::alloc_write_cache(size_t size)
{
    size_t need = cache_write_cursor_ + size;
    if (need > get_cache_write_size())
    {
        flush_write_cache();
        need = cache_write_cursor_ + size;
        if (need > get_cache_write_size())
        {
            size_t new_size = std::max(get_cache_write_size(), size_t(1));
            while (new_size < need)
            {
                new_size <<= 1;
            }
            resize_cache_write_entity(new_size + cache_write_padding_);
        }
    }

    write_with_cache_handle out;
    out.data_ = get_cache_write_ptr_base() + static_cast<ptrdiff_t>(cache_write_cursor_);
    out.alloc_len_ = size;
    out.used_len_ = size;
    return out;
}

void appender_file_base::return_write_cache(const write_with_cache_handle& handle)
{
    cache_write_cursor_ += std::min(handle.used_len_, handle.alloc_len_);
}

size_t appender_file_base::direct_write(const void* data, size_t size, int whence, int64_t seek_offset)
{
    if (!file_)
    {
        return 0;
    }

    std::fseek(file_, static_cast<long>(seek_offset), whence);
    const auto real = std::fwrite(data, 1, size, file_);
    std::fseek(file_, 0, SEEK_END);
    current_file_size_ = static_cast<uint64_t>(std::ftell(file_));
    return real;
}

void appender_file_base::mark_write_finished()
{
    cache_write_finished_cursor_ = cache_write_cursor_;
}

void appender_file_base::set_cache_write_padding(uint8_t new_padding)
{
    if (cache_write_padding_ == new_padding)
    {
        return;
    }

    auto* old_base = get_cache_write_ptr_base();
    const size_t old_padding = cache_write_padding_;
    cache_write_padding_ = new_padding;

    if (cache_write_entity_.size() < cache_write_cursor_ + cache_write_padding_)
    {
        resize_cache_write_entity(cache_write_cursor_ + cache_write_padding_);
    }
    auto* new_base = get_cache_write_ptr_base();

    if (old_base != new_base && cache_write_cursor_ > 0)
    {
        std::memmove(new_base, old_base, cache_write_cursor_);
    }

    if (old_padding > cache_write_padding_ && cache_write_entity_.size() > k_cache_write_default_size)
    {
        resize_cache_write_entity(std::max(k_cache_write_default_size, cache_write_cursor_ + cache_write_padding_));
    }
}

bool appender_file_base::on_appender_file_recovery_begin()
{
    return true;
}

void appender_file_base::on_appender_file_recovery_end()
{
}

void appender_file_base::set_basic_configs(const appender_config& config)
{
    config_file_name_ = config.file_name;
    always_create_new_file_ = config.always_create_new_file;
    max_file_size_ = config.max_file_size;
    enable_rolling_log_file_ = config.enable_rolling_log_file;
    capacity_limit_ = config.capacity_limit;

    if (config.expire_time_seconds > 0)
    {
        expire_time_ms_ = config.expire_time_seconds * 1000;
    }
    else if (config.expire_time_days > 0)
    {
        expire_time_ms_ = config.expire_time_days * 24 * 3600 * 1000;
    }
    else
    {
        expire_time_ms_ = 0;
    }
}

void appender_file_base::refresh_file_handle(const entry_runtime_view& view)
{
    bool need_create_new_file = (file_ == nullptr) || is_file_oversize();
    if (!need_create_new_file && enable_rolling_log_file_)
    {
        const auto epoch = get_entry_epoch_ms(view);
        if (epoch > current_file_expire_time_epoch_ms_)
        {
            need_create_new_file = true;
        }
    }

    if (!need_create_new_file)
    {
        return;
    }

    const auto epoch = get_entry_epoch_ms(view);
    const uint64_t ms_per_day = 24ull * 3600ull * 1000ull;
    const auto local_epoch = static_cast<int64_t>(epoch) + static_cast<int64_t>(gmt_offset_minutes_) * 60 * 1000;
    const auto local_next_day = (static_cast<uint64_t>(local_epoch) / ms_per_day + 1) * ms_per_day;
    current_file_expire_time_epoch_ms_ =
        local_next_day - static_cast<uint64_t>(static_cast<int64_t>(gmt_offset_minutes_) * 60 * 1000);

    if (file_)
    {
        flush_write_cache();
        flush_write_io();
    }
    clear_read_cache();
    open_new_indexed_file_by_name();
}

bool appender_file_base::open_file_with_write_exclusive(const std::string& file_path)
{
    if (file_)
    {
        std::fclose(file_);
        file_ = nullptr;
    }

    file_ = std::fopen(file_path.c_str(), "rb+");
    if (!file_)
    {
        file_ = std::fopen(file_path.c_str(), "wb+");
    }
    if (!file_)
    {
        return false;
    }

    std::fseek(file_, 0, SEEK_END);
    current_file_size_ = static_cast<uint64_t>(std::ftell(file_));
    current_file_path_ = file_path;
    return true;
}

void appender_file_base::resize_cache_write_entity(size_t new_size)
{
    if (new_size == 0)
    {
        new_size = cache_write_padding_ + 1;
    }
    if (new_size < cache_write_padding_ + cache_write_cursor_)
    {
        new_size = cache_write_padding_ + cache_write_cursor_;
    }
    cache_write_entity_.resize(new_size);
}

void appender_file_base::clean_cache_write()
{
    cache_write_cursor_ = 0;
    cache_write_finished_cursor_ = 0;
    cache_write_padding_ = 0;
}

void appender_file_base::open_new_indexed_file_by_name()
{
    if (config_file_name_.empty())
    {
        return;
    }

    if (file_)
    {
        std::fclose(file_);
        file_ = nullptr;
    }
    clean_cache_write();
    clear_all_expired_files();
    clear_all_limit_files();

    std::filesystem::path base(config_file_name_);
    const auto dir = base.parent_path().empty() ? std::filesystem::path(".") : base.parent_path();
    const auto stem = base.filename().string();
    const auto ext = get_file_ext_name();

    std::filesystem::create_directories(dir);

    const auto epoch = now_epoch_ms();
    std::time_t tt = static_cast<std::time_t>(epoch / 1000);
    std::tm tmv {};
#if defined(_WIN32)
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    char day_buf[32] {};
    std::strftime(day_buf, sizeof(day_buf), "_%Y%m%d_", &tmv);
    const std::string prefix = stem + (enable_rolling_log_file_ ? day_buf : "_");

    int max_index = 0;
    for (const auto& it : std::filesystem::directory_iterator(dir))
    {
        if (!it.is_regular_file())
        {
            continue;
        }
        const auto file_name = it.path().filename().string();
        if (file_name.rfind(prefix, 0) != 0)
        {
            continue;
        }
        if (file_name.size() < prefix.size() + ext.size())
        {
            continue;
        }
        if (file_name.substr(file_name.size() - ext.size()) != ext)
        {
            continue;
        }
        const auto idx_str =
            file_name.substr(prefix.size(), file_name.size() - prefix.size() - ext.size());
        const int idx = std::atoi(idx_str.c_str());
        if (idx > max_index)
        {
            max_index = idx;
        }
    }

    if (always_create_new_file_ || max_index == 0)
    {
        ++max_index;
    }

    while (true)
    {
        const auto path = (dir / (prefix + std::to_string(max_index) + ext)).string();
        ++max_index;
        if (!open_file_with_write_exclusive(path))
        {
            continue;
        }
        if (is_file_oversize())
        {
            continue;
        }
        if (current_file_size_ > 0)
        {
            parse_file_context ctx(path);
            if (!parse_exist_log_file(ctx))
            {
                continue;
            }
        }
        break;
    }

    std::fseek(file_, 0, SEEK_END);
    on_file_open(current_file_size_ == 0);
}

bool appender_file_base::is_file_oversize() const
{
    return max_file_size_ > 0 && current_file_size_ >= max_file_size_;
}

void appender_file_base::clear_all_expired_files()
{
    if (expire_time_ms_ == 0 || config_file_name_.empty())
    {
        return;
    }

    std::filesystem::path base(config_file_name_);
    const auto dir = base.parent_path().empty() ? std::filesystem::path(".") : base.parent_path();
    const auto stem = base.filename().string() + "_";
    const auto ext = get_file_ext_name();
    const auto now_ms = now_epoch_ms();

    if (!std::filesystem::exists(dir))
    {
        return;
    }

    for (const auto& it : std::filesystem::directory_iterator(dir))
    {
        if (!it.is_regular_file())
        {
            continue;
        }
        const auto name = it.path().filename().string();
        if (name.rfind(stem, 0) != 0)
        {
            continue;
        }
        if (name.size() < ext.size() || name.substr(name.size() - ext.size()) != ext)
        {
            continue;
        }
        const auto mt = std::filesystem::last_write_time(it.path());
        const auto mt_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(mt.time_since_epoch()).count()
        );
        if (mt_ms + expire_time_ms_ <= now_ms)
        {
            std::error_code ec;
            std::filesystem::remove(it.path(), ec);
        }
    }
}

void appender_file_base::clear_all_limit_files()
{
    if (capacity_limit_ == 0 || config_file_name_.empty())
    {
        return;
    }

    std::filesystem::path base(config_file_name_);
    const auto dir = base.parent_path().empty() ? std::filesystem::path(".") : base.parent_path();
    const auto stem = base.filename().string() + "_";
    const auto ext = get_file_ext_name();

    if (!std::filesystem::exists(dir))
    {
        return;
    }

    struct file_info
    {
        uint64_t mtime = 0;
        std::filesystem::path path {};
        uint64_t size = 0;
    };

    std::vector<file_info> files;
    uint64_t total = 0;

    for (const auto& it : std::filesystem::directory_iterator(dir))
    {
        if (!it.is_regular_file())
        {
            continue;
        }
        const auto name = it.path().filename().string();
        if (name.rfind(stem, 0) != 0)
        {
            continue;
        }
        if (name.size() < ext.size() || name.substr(name.size() - ext.size()) != ext)
        {
            continue;
        }

        const auto sz = static_cast<uint64_t>(it.file_size());
        const auto mt = std::filesystem::last_write_time(it.path());
        const auto mt_ms = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(mt.time_since_epoch()).count()
        );

        files.push_back(file_info { mt_ms, it.path(), sz });
        total += sz;
    }

    if (total < capacity_limit_)
    {
        return;
    }

    std::sort(files.begin(), files.end(), [](const auto& l, const auto& r) {
        return l.mtime < r.mtime;
    });

    for (const auto& f : files)
    {
        std::error_code ec;
        std::filesystem::remove(f.path, ec);
        if (!ec)
        {
            total -= f.size;
        }
        if (total < capacity_limit_)
        {
            break;
        }
    }
}

void appender_file_base::parse_file_context::log_parse_fail_reason(const char* msg) const
{
    std::fprintf(stderr, "failed to parse log file:\"%s\", msg:%s\n", file_name_.c_str(), msg ? msg : "");
}

} // namespace qlog::appender
```
