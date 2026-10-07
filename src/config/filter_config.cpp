#include "qlog/config/filter_config.hpp"

#include <cassert>
#include <cstdio>
#include <utility>

#include "qlog/utility/string_utils.hpp"

namespace qlog::config {
bool get_categories_mask_by_config(const std::vector<std::string> categories_name,
                                   const PropertyValue& categories_mask_config,
                                   std::vector<std::uint8_t>& out_categories_mask) {
    assert(categories_name.size() == out_categories_mask.size());

    std::vector<std::string> tmp;

    if (categories_mask_config.is_array()) {
        for (PropertyValue::array_type::size_type i = 0; i < categories_mask_config.array_size();
             ++i) {
            if (categories_mask_config[i].is_string()) {
                std::string mask = static_cast<std::string>(categories_mask_config[i]);

                tmp.push_back(std::move(mask));
            }
        }
    }

    tmp.reserve(categories_name.size());

    for (std::size_t i = 0; i < categories_name.size(); ++i) {
        const auto& category_name = categories_name[i];
        std::uint8_t mask = 0;

        if (tmp.empty()) {
            mask = 1;
        } else {
            for (const std::string& mask_config : tmp) {
                if (category_name == mask_config ||
                    (category_name.size() > mask_config.size() &&
                     category_name.compare(0, mask_config.size(), mask_config) == 0 &&
                     category_name[mask_config.size()] == '.')) {
                    mask = 1;
                    break;
                }

                if (i == 0 && mask_config == "*default") {
                    mask = 1;
                    break;
                }
            }
        }

        out_categories_mask[i] = mask;
    }

    return true;
}

bool get_log_level_bitmap_by_config(const PropertyValue& log_level_bitmap_config,
                                    qlog::runtime::LogLevelBitmap& out_level_bitmap) {
    qlog::runtime::LogLevelBitmap tmp;

    if (!log_level_bitmap_config.is_array()) {
        out_level_bitmap.clear();
        return false;
    }

    for (PropertyValue::array_type::size_type i = 0; i < log_level_bitmap_config.array_size();
         ++i) {
        const auto& level_obj = log_level_bitmap_config[i];

        if (!level_obj.is_string()) {
            std::fprintf(stderr, "qlog warning: invalid [log_level] item: %s\n",
                         level_obj.serialize().c_str());

            continue;
        }

        tmp.add_level(qlog::utility::trim(static_cast<std::string>(level_obj)));
    }

    out_level_bitmap = tmp;
    return true;
}
}  // namespace qlog::config