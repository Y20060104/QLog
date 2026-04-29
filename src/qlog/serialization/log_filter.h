#pragma once

#include "qlog/primitives/atomic.h"
#include "qlog/serialization/entry_format.h"

#include <cstdint>

namespace qlog::serialization
{
inline constexpr uint32_t MAX_CATEGORIES = 128; // 最大支持分类数

class alignas(64) log_filter
{
public:
    log_filter();
    ~log_filter() = default;

    // 禁用拷贝
    log_filter(const log_filter&) = delete;
    log_filter& operator=(const log_filter&) = delete;

    // 写端：配置更新 冷路径 允许开销
    // bitmap bit[i]=1 表示level(1)启用
    void set_level_bitmap(uint8_t bitmap) noexcept;

    // 启用/禁用单个category(0=disabled 1=enabled)
    void set_category_enabled(uint32_t category_idx, bool enabled) noexcept;

    // 批量初始化 category_masks
    void set_all_categories_enabled(bool enabled) noexcept;

    // 读端：快速过滤（热路径）
    [[nodiscard]] bool is_enabled(uint32_t category_idx, log_level level) const noexcept;

    // 获取当前 level bitmap
    [[nodiscard]] uint8_t level_bitmap() const noexcept;

private:
    atomic<uint8_t> level_bitmap_;

    // category masks：单字节原子写少读多
    // 对齐到 cache line，与 level_bitmap_ 分离（防 false sharing）
    alignas(64) atomic<uint8_t> category_masks_[MAX_CATEGORIES];
};

// 热路径内联实现
// 两个relaxed_load+位运算
inline bool log_filter::is_enabled(uint32_t category_idx, log_level level) const noexcept
{
    if (category_idx >= MAX_CATEGORIES) [[unlikely]]
    {
        return false;
    }

    const uint8_t lvl = static_cast<uint8_t>(level);
    if (lvl >= static_cast<uint8_t>(log_level::count)) [[unlikely]]
    {
        return false;
    }

    // relaxed 足够：过滤结果不影响共享数据的有序性
    // 最坏情况：配置更新后"多记录一条"或"少记录一条"，可接受
    const uint8_t bitmap = level_bitmap_.load(std::memory_order_relaxed);
    const uint8_t cat_mask = category_masks_[category_idx].load(std::memory_order_relaxed);

    return ((bitmap & (uint8_t{1} << lvl)) != 0u) & (cat_mask != 0u);
}
} // namespace qlog::serialization