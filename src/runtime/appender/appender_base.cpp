/* Copyright (C) 2025 Tencent.
 * BQLOG is licensed under the Apache License, Version 2.0.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 */
#include "qlog/runtime/appender/appender_base.hpp"

#include "qlog/config/filter_config.hpp"
#include "qlog/global/log_vars.hpp"
#include "qlog/runtime/log_imp.hpp"
#include "qlog/runtime/log_manager.hpp"

namespace qlog::runtime {
appender_base::appender_base() {
    clear();
}

appender_base::~appender_base() {
    clear();
}

std::string appender_base::get_config_name_by_type(const appender_base::appender_type type) {
    return log_global_vars::get().log_appender_type_names_[static_cast<int32_t>(type)];
}

void appender_base::set_enable(bool enable) {
    appenders_enable = enable;
}

bool appender_base::get_enable() {
    return appenders_enable;
}

void appender_base::clear() {
    time_zone_.reset();
    log_level_bitmap_.clear();
    parent_log_ = nullptr;
    layout_ptr_ = nullptr;
    name_.clear();
    categories_mask_array_.clear();
    categories_mask_config_.clear();
}

bool appender_base::init(const std::string& name, const qlog::runtime::property_value& config_obj,
                         const log_imp* parent_log) {
    clear();
    parent_log_ = parent_log;
    name_ = name;
    set_basic_configs(config_obj);
    return init_impl(config_obj);
}

bool appender_base::reset(const qlog::runtime::property_value& config_obj) {
    std::string type_str = config_obj["type"].is_string() ? (std::string)(config_obj["type"]) : "";
    if (!qlog::utility::equals_ignore_case(get_config_name_by_type(type_), type_str)) {
        return false;
    }
    auto parent_log_backup = parent_log_;
    auto name_backup = name_;
    clear();
    parent_log_ = parent_log_backup;
    name_ = name_backup;
    set_basic_configs(config_obj);
    return reset_impl(config_obj);
}

bool appender_base::log(const log_entry_handle& handle) {
    auto category_idx = handle.get_log_head().category_idx;
    if (categories_mask_array_.size() <= category_idx ||
        categories_mask_array_[category_idx] == 0) {
        return false;
    }

    if (log_level_bitmap_.have_level(handle.get_level())) {
        if (appenders_enable) {
            return log_impl(handle);
        }
    }
    return false;
}

void appender_base::set_basic_configs(const qlog::runtime::property_value& config_obj) {
    const auto& levels_array = config_obj["levels"];
    if (!qlog::config::get_log_level_bitmap_by_config(levels_array, log_level_bitmap_)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::info,
                                                "bq log info: no levels config was found in "
                                                "appender type %s, use default level \"all\"",
                                                ((string)config_obj["type"]).c_str());
        log_level_bitmap_.add_level("all");
    }

    if (config_obj["time_zone"].is_string()) {
        std::string time_zone_str =
            qlog::utility::trim(static_cast<std::string>(config_obj["time_zone"]));
        time_zone_.parse_by_string(time_zone_str);
    }

    if (config_obj["enable"].is_bool()) {
        appenders_enable = (bool)config_obj["enable"];
    }

    // config verification is already done in log_imp::add_appender
    std::string type_str = qlog::utility::trim(static_cast<std::string>(config_obj["type"]));
    for (int32_t i = 0; i < (int32_t)appender_base::appender_type::type_count; ++i) {
        if (qlog::utility::equals_ignore_case(type_str, appender_base::get_config_name_by_type(
                                                            (appender_base::appender_type)i))) {
            type_ = (appender_base::appender_type)i;
            break;
        }
    }

    // init categories mask
    {
        categories_mask_config_.clear();
        const auto& categories_mask_config = config_obj["categories_mask"];
        if (categories_mask_config.is_array()) {
            for (typename property_value::array_type::size_type i = 0;
                 i < categories_mask_config.array_size(); ++i) {
                if (categories_mask_config[i].is_string()) {
                    std::string mask = (string)categories_mask_config[i];
                    categories_mask_config_.push_back(std::move(mask));
                }
            }
        }
    }

    categories_mask_array_.clear();
    auto& categories_name_array_ = parent_log_->get_categories_name();
    for (size_t i = 0; i < categories_name_array_.size(); ++i) {
        const auto& category_name = categories_name_array_[i];
        uint8_t mask = 0;
        if (categories_mask_config_.size() == 0) {
            mask = 1;
        } else {
            for (const std::string& mask_config : categories_mask_config_) {
                if (mask_config == "*" || category_name.starts_with(mask_config)) {
                    mask = 1;
                    break;
                }
            }
        }
        categories_mask_array_.push_back(mask);
    }
    if (parent_log_->get_thread_mode() == log_thread_mode::async) {
        layout_ptr_ = &log_manager::instance().get_public_layout();
    } else {
        layout_ptr_ = const_cast<layout*>(&(parent_log_->get_layout()));
    }
}
}  // namespace qlog::runtime
