#pragma once

#include <string>
#include <vector>

#include "qlog/buffer/log_buffer_config.hpp"
#include "qlog/config/property_value.hpp"

namespace qlog::config {
qlog::buffer::LogBufferConfig get_log_buffer_config_by_config(
    const std::string& log_name, const std::vector<std::string>& log_categories_name,
    const PropertyValue& log_config,
    bool memory_map_supported);  // CR:为什么log config可以用propertyvalue这个类型 这个类型是什么
                                 // 记录了什么？为什么要这样？
}