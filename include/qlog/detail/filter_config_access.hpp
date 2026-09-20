#pragma once
#include "qlog/detail/filter_state.hpp"
namespace qlog::detail {
class FilterConfigAccess final {
   public:
    static void set_category(FilterState& f, std::uint32_t id, bool enabled) noexcept {
        f.publish_category(id, enabled);
    }
    static void set_levels(FilterState& f, std::uint32_t levels) noexcept {
        f.publish_merged_levels(levels);
    }
};
}  // namespace qlog::detail
