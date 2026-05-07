# M6 `appender_base.h` / `appender_base.cpp`

## `src/qlog/appender/appender_base.h`

```cpp
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
    std::string_view format_string {};
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
    appender_type type = appender_type::console;
    bool enable = true;
    uint8_t level_bitmap = 0x3F; // verbose..fatal 全开
    std::vector<std::string> categories_mask {};

    bool use_local_time = true;
    int32_t gmt_offset_minutes = 0;

    std::string file_name {};
    bool always_create_new_file = false;
    uint64_t max_file_size = 0;
    bool enable_rolling_log_file = true;
    uint64_t expire_time_seconds = 0;
    uint64_t expire_time_days = 0;
    uint64_t capacity_limit = 0;

    std::string public_key {};
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
    virtual ~appender_base();

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

protected:
    virtual bool init_impl(const appender_config& config) = 0;
    virtual bool reset_impl(const appender_config& config) = 0;
    virtual void log_impl(const entry_runtime_view& view) = 0;

    virtual void on_log_item_recovery_begin(entry_runtime_view&) {}
    virtual void on_log_item_recovery_end() {}
    virtual void on_log_item_new_begin(entry_runtime_view&) {}

    [[nodiscard]] bool is_category_enabled(uint16_t category_idx) const noexcept;
    [[nodiscard]] bool is_level_enabled(serialization::log_level level) const noexcept;

protected:
    bool use_local_time_ = true;
    int32_t gmt_offset_minutes_ = 0;
    layout::layout* layout_ptr_ = nullptr;
    appender_type type_ = appender_type::console;
    bool appenders_enable_ = true;
    std::vector<std::string> categories_mask_config_ {};
    std::vector<uint8_t> categories_mask_array_ {};
    const std::vector<std::string>* categories_name_ = nullptr;

private:
    void set_basic_configs(const appender_config& config);
    void rebuild_categories_mask();

private:
    log_level_bitmap log_level_bitmap_ {};
    std::string name_ {};
};

} // namespace qlog::appender
```

## `src/qlog/appender/appender_base.cpp`

```cpp
#include "qlog/appender/appender_base.h"

#include <algorithm>
#include <cctype>

namespace qlog::appender
{

namespace
{
std::string to_lower_copy(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}
} // namespace

appender_base::~appender_base()
{
    clear();
}

std::string appender_base::config_name_by_type(appender_type type)
{
    switch (type)
    {
    case appender_type::console:
        return "console";
    case appender_type::text_file:
        return "text_file";
    case appender_type::raw_file:
        return "raw_file";
    case appender_type::compressed_file:
        return "compressed_file";
    default:
        return "unknown";
    }
}

void appender_base::clear()
{
    use_local_time_ = true;
    gmt_offset_minutes_ = 0;
    layout_ptr_ = nullptr;
    appenders_enable_ = true;
    type_ = appender_type::console;
    categories_mask_config_.clear();
    categories_mask_array_.clear();
    categories_name_ = nullptr;
    name_.clear();
    log_level_bitmap_.clear();
    log_level_bitmap_.set_bitmap(0x3F);
}

bool appender_base::init(
    const std::string& name,
    const appender_config& config,
    layout::layout* layout_ptr,
    const std::vector<std::string>* categories_name
)
{
    clear();
    name_ = name;
    layout_ptr_ = layout_ptr;
    categories_name_ = categories_name;
    set_basic_configs(config);
    return init_impl(config);
}

bool appender_base::reset(const appender_config& config)
{
    const auto type_backup = type_;
    const auto name_backup = name_;
    const auto layout_backup = layout_ptr_;
    const auto categories_backup = categories_name_;

    clear();
    type_ = type_backup;
    name_ = name_backup;
    layout_ptr_ = layout_backup;
    categories_name_ = categories_backup;
    set_basic_configs(config);
    return reset_impl(config);
}

void appender_base::set_basic_configs(const appender_config& config)
{
    type_ = config.type;
    appenders_enable_ = config.enable;
    use_local_time_ = config.use_local_time;
    gmt_offset_minutes_ = config.gmt_offset_minutes;
    categories_mask_config_ = config.categories_mask;
    log_level_bitmap_.set_bitmap(config.level_bitmap);
    rebuild_categories_mask();
}

void appender_base::rebuild_categories_mask()
{
    categories_mask_array_.clear();
    if (!categories_name_)
    {
        return;
    }

    categories_mask_array_.resize(categories_name_->size(), uint8_t(1));
    if (categories_mask_config_.empty())
    {
        return;
    }

    std::vector<std::string> lower_masks;
    lower_masks.reserve(categories_mask_config_.size());
    for (const auto& m : categories_mask_config_)
    {
        lower_masks.push_back(to_lower_copy(m));
    }

    for (size_t i = 0; i < categories_name_->size(); ++i)
    {
        const auto category = to_lower_copy((*categories_name_)[i]);
        bool enabled = false;
        for (const auto& mask : lower_masks)
        {
            if (mask == "*")
            {
                enabled = true;
                break;
            }
            if (category.rfind(mask, 0) == 0)
            {
                enabled = true;
                break;
            }
        }
        categories_mask_array_[i] = enabled ? uint8_t(1) : uint8_t(0);
    }
}

bool appender_base::is_category_enabled(uint16_t category_idx) const noexcept
{
    if (category_idx >= categories_mask_array_.size())
    {
        return false;
    }
    return categories_mask_array_[category_idx] != 0;
}

bool appender_base::is_level_enabled(serialization::log_level level) const noexcept
{
    return log_level_bitmap_.have_level(level);
}

void appender_base::log(const entry_runtime_view& view)
{
    if (!appenders_enable_)
    {
        return;
    }

    const auto* hdr = view.header();
    if (!hdr)
    {
        return;
    }

    const auto lv = static_cast<serialization::log_level>(hdr->level);
    if (!is_level_enabled(lv))
    {
        return;
    }
    if (!is_category_enabled(hdr->category_idx))
    {
        return;
    }
    log_impl(view);
}

} // namespace qlog::appender
```

