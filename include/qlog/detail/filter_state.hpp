#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace qlog::detail {
class FilterConfigAccess;
class FilterState final {
   public:
    explicit FilterState(std::uint32_t initial_merged_levels,
                         const std::uint8_t* initial_categories, std::size_t category_count);
    ~FilterState() = default;
    FilterState(const FilterState&) = delete;
    FilterState& operator=(const FilterState&) = delete;
    FilterState(FilterState&&) = delete;
    FilterState& operator=(FilterState&&) = delete;

    [[nodiscard]] std::uint32_t category_count() const noexcept {
        return category_count_;
    }

    [[nodiscard]] bool category_enabled_unchecked(std::uint32_t id) const noexcept {
        assert(id < category_count_);
        return category_enabled_[id].load(std::memory_order_relaxed) != 0;
    }

    [[nodiscard]] bool allows_unchecked(std::uint32_t category_id,
                                        std::uint8_t level) const noexcept {
        assert(category_id < category_count_ && level < 6U);
        const auto bitmap = merged_levels_.load(std::memory_order_relaxed);
        return (bitmap & (std::uint32_t{1} << level)) != 0U &&
               category_enabled_unchecked(category_id);
    }

   private:
    friend class FilterConfigAccess;
    void publish_merged_levels(std::uint32_t bitmap) noexcept;
    void publish_category(std::uint32_t id, bool enabled) noexcept;
    const std::uint32_t category_count_{};
    std::unique_ptr<std::atomic<std::uint8_t>[]> category_enabled_;

    std::atomic<std::uint32_t> merged_levels_;
};
}  // namespace qlog::detail