#include "qlog/serialization/log_filter.h"

namespace qlog::serialization
{

log_filter::log_filter()
{
    // 默认全部级别启用 所有categories启用
    // bitmap 低6位全1=0x3F
    level_bitmap_.store(0x3F, std::memory_order_relaxed);

    for (uint32_t i = 0; i < MAX_CATEGORIES; ++i)
    {
        category_masks_[i].store(1, std::memory_order_relaxed);
    }
}

void log_filter::set_level_bitmap(uint8_t bitmap) noexcept
{
    // release 确保调用方先于 bitmap 更新完成
    level_bitmap_.store(bitmap, std::memory_order_release);
}

void log_filter::set_category_enabled(uint32_t category_idx, bool enabled) noexcept
{
    if (category_idx >= MAX_CATEGORIES) [[unlikely]]
    {
        return;
    }

    category_masks_[category_idx].store(
        enabled ? uint8_t{1} : uint8_t{0}, std::memory_order_release
    );
}

void log_filter::set_all_categories_enabled(bool enabled) noexcept
{
    const uint8_t val = enabled ? uint8_t{1} : uint8_t{0};
    for (uint32_t i = 0; i < MAX_CATEGORIES; ++i)
    {
        category_masks_[i].store(val, std::memory_order_relaxed);
    }

    std::atomic_thread_fence(std::memory_order_release);
}

uint8_t log_filter::level_bitmap() const noexcept
{
    return level_bitmap_.load(std::memory_order_relaxed);
}

} // namespace qlog::serialization