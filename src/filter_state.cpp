#include "qlog/detail/filter_state.hpp"

#include <limits>
#include <stdexcept>

namespace qlog::detail {
namespace {
std::uint32_t validate_initial_filter(std::uint32_t merged, const std::uint8_t* categories,
                                      std::size_t category_count) {
    if ((merged & ~std::uint32_t{0x3F}) != 0U || category_count == 0U || categories == nullptr ||
        category_count > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("invalid filter metadata");
    }

    for (std::size_t i = 0; i < category_count; ++i) {
        if (categories[i] > 1U) {
            throw std::invalid_argument("category value must be 0 or 1");
        }
    }

    return static_cast<std::uint32_t>(category_count);
}
}  // namespace

FilterState::FilterState(std::uint32_t merged, const std::uint8_t* categories,
                         std::size_t category_count)
    : category_count_(validate_initial_filter(merged, categories, category_count)),
      category_enabled_(std::make_unique<std::atomic<std::uint8_t>[]>(category_count_)),
      merged_levels_(merged) {
    for (std::uint32_t i = 0; i < category_count_; ++i) {
        category_enabled_[i].store(categories[i], std::memory_order_relaxed);
    }
}

void FilterState::publish_category(std::uint32_t category_id, bool enabled) noexcept {
    assert(category_id < category_count_);
    category_enabled_[category_id].store(static_cast<std::uint8_t>(enabled),
                                         std::memory_order_relaxed);
}

void FilterState::publish_merged_levels(std::uint32_t bitmap) noexcept {
    assert((bitmap & ~0x3FU) == 0U);
    merged_levels_.store(bitmap, std::memory_order_relaxed);
}
}  // namespace qlog::detail