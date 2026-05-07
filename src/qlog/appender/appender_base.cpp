#include "qlog/appender/appender_base.h"

#include <algorithm>
#include <cctype>

namespace qlog::appender
{
namespace
{
std::string to_lower_copy(std::string s)
{
    std::transform(
        s.begin(),
        s.end(),
        s.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        }
    );
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
    ;
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