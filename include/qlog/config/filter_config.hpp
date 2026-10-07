#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "qlog/config/property_value.hpp"
#include "qlog/runtime/log_level_bitmap.hpp"
namespace qlog::config {
bool get_categories_mask_by_config(const std::vector<std::string> categories_name,
                                   const PropertyValue& categories_mask_config,
                                   std::vector<std::uint8_t>& out_categories_mask);

bool get_log_level_bitmap_by_config(const PropertyValue& log_level_bitmap_config,
                                    qlog::runtime::LogLevelBitmap& out_level_bitmap);
}  // namespace qlog::config