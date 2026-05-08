#pragma once

#include "qlog/layout/layout.h"
#include "qlog/serialization/entry_format.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace qlog::appender
{

struct entry_runtime_view
{
    const uint8_t* entry_data = nullptr;
    uint32_t entry_size = 0;
    std::string_view format_string{};
    const char* const* categories = nullptr;
    uint32_t category_count = 0;
    uint64_t log_id = 0;

    const serialization::entry_header* header() const noexcept
    {
        if (!entry_data || entry_size < sizeof(serialization::entry_header))
        {
            return nullptr;
        }
        return reinterpret_cast<const serialization::entry_header*>(entry_data);
    }
};

enum class appender_type : uint8_t
{
    console = 0,
    text_file = 1,
    raw_file = 2,
    compressed_file = 3,
    type_count = 4
};

struct appender_config
{
    appender_type type = appender_type::console; // 默认
    bool enable = true;
    uint8_t level_bitmap = 0x3F; // verbose..fatal全开
    std::vector<std::string> categories_mask{};

    bool use_local_time = true;
    int32_t gmt_offset_minutes = 0;

    std::string file_name{};
    bool always_create_new_file = false;
    uint64_t max_file_size = 0;
    bool enable_rolling_log_file = true;
    uint64_t expire_time_seconds = 0;
    uint64_t expire_time_days = 0;
    uint64_t capacity_limit = 0;

    std::string public_key{};
};

class log_level_bitmap
{
public:
    void clear() noexcept
    {
        bitmap_ = 0;
    }

    void set_bitmap(uint8_t bm) noexcept
    {
        bitmap_ = bm;
    }

    [[nodiscard]] uint8_t bitmap() const noexcept
    {
        return bitmap_;
    }

    [[nodiscard]] bool have_level(serialization::log_level level) const noexcept
    {
        const uint8_t lv = static_cast<uint8_t>(level);
        if (lv >= static_cast<uint8_t>(serialization::log_level::count))
        {
            return false;
        }
        return (bitmap_ & (uint8_t(1) << lv)) != 0;
    }

private:
    uint8_t bitmap_ = 0x3F;
};

class appender_base
{
public:
    appender_base() = default;
    virtual ~appender_base(); // 虚析构函数 一并析构 防止遗漏

    static std::string config_name_by_type(appender_type type);

    bool init(
        const std::string& name,
        const appender_config& config,
        layout::layout* layout_ptr,
        const std::vector<std::string>* categories_name
    );

    bool reset(const appender_config& config);
    void clear();
    void log(const entry_runtime_view& view);

    void set_enable(bool enable) noexcept
    {
        appenders_enable_ = enable;
    }

    [[nodiscard]] bool get_enable() const noexcept
    {
        return appenders_enable_;
    }

    [[nodiscard]] log_level_bitmap get_log_level_bitmap() const noexcept
    {
        return log_level_bitmap_;
    }

    [[nodiscard]] const std::string& get_name() const noexcept
    {
        return name_;
    }

    [[nodiscard]] appender_type get_type() const noexcept
    {
        return type_;
    }

    virtual void flush() {}  // 默认空实现；文件 appender 覆盖此方法

protected:
    virtual bool init_impl(const appender_config& config) = 0;
    virtual bool reset_impl(const appender_config& config) = 0;
    virtual void log_impl(const entry_runtime_view& view) = 0;

    virtual void on_log_item_recovery_begin(entry_runtime_view&)
    {
    }
    virtual void on_log_item_recovery_end()
    {
    }
    virtual void on_log_item_new_begin(entry_runtime_view&)
    {
    }
    [[nodiscard]] bool is_category_enabled(uint16_t category_idx) const noexcept;
    [[nodiscard]] bool is_level_enabled(serialization::log_level level) const noexcept;

protected:
    bool use_local_time_ = true;
    int32_t gmt_offset_minutes_ = 0;
    layout::layout* layout_ptr_ = nullptr;
    appender_type type_ = appender_type::console;
    bool appenders_enable_ = true;
    std::vector<std::string> categories_mask_config_{};
    std::vector<uint8_t> categories_mask_array_{};
    const std::vector<std::string>* categories_name_ = nullptr;

private:
    void set_basic_configs(const appender_config& config);
    void rebuild_categories_mask();

private:
    log_level_bitmap log_level_bitmap_{};
    std::string name_;
};
} // namespace qlog::appender