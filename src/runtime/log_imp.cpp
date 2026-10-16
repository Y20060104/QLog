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
#include "qlog/runtime/log_imp.hpp"

#include "qlog/config/filter_config.hpp"
#include "qlog/runtime/appender/appender_console.hpp"
#include "qlog/runtime/appender/appender_file_text.hpp"
#include "qlog/runtime/log_snapshot.hpp"
#include "qlog/runtime/log_types.hpp"

namespace qlog::runtime {

class sync_buffer {
   private:
    std::vector<uint8_t> buffer_;
    size_t default_buffer_size_;

   public:
    sync_buffer() : default_buffer_size_(4 * 1024) {}
    ~sync_buffer() {}
    void set_default_buffer_size(size_t default_buffer_size) {
        default_buffer_size_ = default_buffer_size;
    }
    qlog_forceinline uint8_t* alloc_data(uint32_t size) {
        buffer_.resize(static_cast<size_t>(size));
        return buffer_.data();
    }

    const qlog_forceinline uint8_t* get_aligned_data() const {
        return buffer_.data();
    }
    qlog_forceinline void recycle_data() {
        if (buffer_.capacity() > default_buffer_size_) {
            buffer_.clear();
            decltype(buffer_) reduced;
            reduced.reserve(default_buffer_size_);
            buffer_.swap(reduced);
        } else {
            buffer_.clear();
        }
    }
    qlog_forceinline bool is_empty() const {
        return buffer_.empty();
    }
    qlog_forceinline uint32_t get_used_data_size() const {
        return static_cast<uint32_t>(buffer_.size());
    }
};
QLOG_TLS_NON_POD(sync_buffer, sync_buffer_)

log_imp::log_imp()
    : id_(0),
      thread_mode_(log_thread_mode::async),
      buffer_(nullptr),
      snapshot_(nullptr),
      last_log_entry_epoch_ms_(0),
      last_flush_io_epoch_ms_(0),
      recover_status_(recover_status_enum::not_started),
      recovery_appenders_need_begin_(false) {}

log_imp::~log_imp() {
    clear();
}

bool log_imp::init(const std::string& name, const property_value& config,
                   const std::vector<std::string>& category_names) {
    id_ = (uint64_t) reinterpret_cast<uintptr_t>(this);
    id_ ^= log_id_magic_number;
    const auto& log_config = config["log"];
    name_ = name;

    qlog::platform::scoped_spin_lock lock(spin_lock_);

    // init thread_mode
    const auto& thread_mode_config = log_config["thread_mode"];
    if (thread_mode_config.is_string()) {
        if ("sync" == (std::string)thread_mode_config) {
            thread_mode_ = log_thread_mode::sync;
        } else if ("async" == (std::string)thread_mode_config) {
            thread_mode_ = log_thread_mode::async;
        } else if ("independent" == (std::string)thread_mode_config) {
            thread_mode_ = log_thread_mode::independent;
        } else {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::warning, "unrecognized thread mode:%s, use async instead.",
                ((std::string)thread_mode_config).c_str());
        }
    }

    categories_name_array_ = category_names;
    // init categories mask
    {
        categories_mask_array_.resize(categories_name_array_.size());
        qlog::config::get_categories_mask_by_config(
            categories_name_array_, log_config["categories_mask"], categories_mask_array_);
    }

    // init print_stack_levels
    {
        qlog::config::get_log_level_bitmap_by_config(log_config["print_stack_levels"],
                                                     print_stack_level_bitmap_);
    }

    {
        auto buffer_config = qlog::config::get_log_buffer_config_by_config(
            name_, categories_name_array_, log_config, qlog::memory_map::is_platform_support());
        buffer_ = qlog::utility::util::aligned_new<qlog::buffer::log_buffer>(
            alignof(qlog::buffer::log_buffer), buffer_config);
    }
    // init appenders
    {
        const auto& all_apenders_config = config["appenders_config"];
        if (!all_apenders_config.is_object()) {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error,
                "create_log parse property failed, invalid appenders_config");
            return false;
        }

        auto appender_names = all_apenders_config.get_object_key_set();
        for (const auto& name_key : appender_names) {
            add_appender(name_key, all_apenders_config[name_key]);
        }
        refresh_merged_log_level_bitmap();
    }

    // init snapshot
    {
        const auto& snapshot_config = config["snapshot"];
        snapshot_ = new log_snapshot(this, snapshot_config);
    }
    worker_.init(thread_mode_, this);
    if (thread_mode_ == log_thread_mode::independent) {
        worker_.start();
    }
    return true;
}

bool log_imp::reset_config(const property_value& config) {
    const auto& log_config = config["log"];

    // init print_stack_levels
    {
        qlog::config::get_log_level_bitmap_by_config(log_config["print_stack_levels"],
                                                     print_stack_level_bitmap_);
    }

    // init or reset appenders
    {
        qlog::platform::scoped_spin_lock lock(spin_lock_);
        const auto& all_apenders_config = config["appenders_config"];
        if (!all_apenders_config.is_object()) {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error,
                "create_log parse property failed, invalid appenders_config");
            return false;
        }
        decltype(recovery_appenders_) previous_recovery_appenders;
        if (recover_status_ == recover_status_enum::in_recovering) {
            previous_recovery_appenders = std::move(recovery_appenders_);
        }
        flush_appenders_cache();
        flush_appenders_io();
        auto appender_names = all_apenders_config.get_object_key_set();
        decltype(appenders_list_) tmp_list = std::move(appenders_list_);
        assert(appenders_list_.size() == 0);
        std::vector<std::string> new_create_appender_names;
        for (const auto& name_key : appender_names) {
            auto old_iter =
                std::find_if(tmp_list.begin(), tmp_list.end(),
                             [&name_key](const std::unique_ptr<appender_base>& appender) {
                                 return appender->get_name() == name_key;
                             });
            if (old_iter != tmp_list.end()) {
                if ((*old_iter)->reset(all_apenders_config[name_key])) {
                    appenders_list_.push_back(std::move(*old_iter));
                    tmp_list.erase(old_iter);
                    continue;
                }
            }
            new_create_appender_names.push_back(name_key);
        }
        if (recover_status_ == recover_status_enum::in_recovering) {
            for (decltype(appenders_list_)::size_type i = 0; i < appenders_list_.size(); ++i) {
                for (decltype(previous_recovery_appenders)::size_type j = 0;
                     j < previous_recovery_appenders.size(); ++j) {
                    if (appenders_list_[i].operator->() == previous_recovery_appenders[j]) {
                        recovery_appenders_.push_back(previous_recovery_appenders[j]);
                        break;
                    }
                }
            }
        }
        tmp_list.clear();
        for (const auto& name : new_create_appender_names) {
            add_appender(name, all_apenders_config[name]);
        }
        if (recover_status_ == recover_status_enum::in_recovering) {
            recovery_appenders_need_begin_ = recovery_appenders_.size() != appenders_list_.size();
        }
        refresh_merged_log_level_bitmap();
    }
    // init categories mask
    {
        qlog::config::get_categories_mask_by_config(
            categories_name_array_, log_config["categories_mask"], categories_mask_array_);
    }

    // init snapshot
    {
        const auto& snapshot_config = config["snapshot"];
        snapshot_->reset_config(snapshot_config);
    }
    return true;
}

void log_imp::set_config(const std::string& config) {
    last_config_ = config;
}

std::string& log_imp::get_config() {
    return last_config_;
}

bool log_imp::add_appender(const string& name, const qlog::runtime::property_value& jobj) {
    const auto& type_obj = jobj["type"];
    if (!type_obj.is_string()) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::error,
            "add_appender failed, appender \"%s\" has invalid \"type\" config", name.c_str());
        return false;
    }
    std::string type_str = qlog::utility::trim(static_cast<std::string>(type_obj));
    for (int32_t i = 0; i < (int32_t)appender_base::appender_type::type_count; ++i) {
        if (qlog::utility::equals_ignore_case(type_str, appender_base::get_config_name_by_type(
                                                            (appender_base::appender_type)i))) {
            break;
        }
    }
    std::unique_ptr<appender_base> appender;
    if (qlog::utility::equals_ignore_case(type_str, appender_base::get_config_name_by_type(
                                                        appender_base::appender_type::console))) {
        appender = std::make_unique<appender_console>();
    } else if (qlog::utility::equals_ignore_case(type_str,
                                                 appender_base::get_config_name_by_type(
                                                     appender_base::appender_type::text_file))) {
        appender = std::make_unique<appender_file_text>();
    } else if (qlog::utility::equals_ignore_case(
                   type_str, appender_base::get_config_name_by_type(
                                 appender_base::appender_type::compressed_file))) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::error,
            "QLog: Appender type compressed_file is not supported in this branch");
        return false;
    } else {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::error, "bq log error: invalid Appender type : %s", type_str.c_str());
        return false;
    }
    if (!appender->init(name, jobj, this)) {
        return false;
    }
    appenders_list_.push_back(std::move(appender));
    return true;
}

void log_imp::clear() {
    appenders_list_.clear();
    recovery_appenders_.clear();
    recovery_appenders_need_begin_ = false;
    categories_name_array_.clear();
    categories_name_array_.clear();
    name_.clear();
    merged_log_level_bitmap_.clear();
    if (buffer_) {
        qlog::utility::util::aligned_delete(buffer_);
    }
    delete snapshot_;
    snapshot_ = nullptr;
    id_ = 0;
}

void log_imp::process_log_chunk(qlog::runtime::log_entry_handle& read_handle) {
    bool is_recovered_entry = false;
    QLOG_UNLIKELY_IF(buffer_->is_current_reading_recovered()) {
        if (!read_handle.validate()) {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::warning,
                "bqlog: corrupted recovered log entry detected and dropped. "
                "data_size=%" PRIu32 ", log=%s",
                read_handle.data_size(), name_.c_str());
            return;
        }
    }

    if (recover_status_ == recover_status_enum::not_started) {
        auto current_version = buffer_->get_current_reading_version();
        auto version = buffer_->get_version();
        if (current_version == version) {
            recover_status_ = recover_status_enum::recovered;
            for (decltype(appenders_list_)::size_type i = 0; i < appenders_list_.size(); i++) {
                appenders_list_[i]->on_log_item_new_begin(read_handle);
            }
        } else {
            recover_status_ = recover_status_enum::in_recovering;
            recovery_appenders_.clear();
            for (decltype(appenders_list_)::size_type i = 0; i < appenders_list_.size(); ++i) {
                if (appenders_list_[i]->on_log_item_recovery_begin(read_handle)) {
                    recovery_appenders_.push_back(appenders_list_[i].operator->());
                }
            }
            recovery_appenders_need_begin_ = false;
            is_recovered_entry = true;
        }
    } else if (recover_status_ == recover_status_enum::in_recovering) {
        auto current_version = buffer_->get_current_reading_version();
        auto version = buffer_->get_version();
        if (current_version == version) {
            recover_status_ = recover_status_enum::recovered;
            for (decltype(recovery_appenders_)::size_type i = 0; i < recovery_appenders_.size();
                 ++i) {
                recovery_appenders_[i]->on_log_item_recovery_end();
            }
            recovery_appenders_.clear();
            recovery_appenders_need_begin_ = false;
            for (decltype(appenders_list_)::size_type i = 0; i < appenders_list_.size(); i++) {
                appenders_list_[i]->on_log_item_new_begin(read_handle);
            }
        } else {
            if (recovery_appenders_need_begin_) {
                for (decltype(appenders_list_)::size_type i = 0; i < appenders_list_.size(); ++i) {
                    bool is_recovery_appender = false;
                    for (decltype(recovery_appenders_)::size_type j = 0;
                         j < recovery_appenders_.size(); ++j) {
                        if (appenders_list_[i].operator->() == recovery_appenders_[j]) {
                            is_recovery_appender = true;
                            break;
                        }
                    }
                    if (!is_recovery_appender &&
                        appenders_list_[i]->on_log_item_recovery_begin(read_handle)) {
                        recovery_appenders_.push_back(appenders_list_[i].operator->());
                    }
                }
                recovery_appenders_need_begin_ = false;
            }
            is_recovered_entry = true;
        }
    }

    auto& head = read_handle.get_log_head();

    // Due to the high concurrency of our ring_buffer,
    // we cannot guarantee that the order of log entries matches the sequence of system time
    // retrieval for each entry. To avoid timestamp regression in such scenarios, we've implemented
    // a minor safeguard.
    if (head.timestamp_epoch > last_log_entry_epoch_ms_) {
        last_log_entry_epoch_ms_ = head.timestamp_epoch;
    } else {
        head.timestamp_epoch = last_log_entry_epoch_ms_;
    }
    QLOG_LIKELY_IF(!is_recovered_entry) {
        log(read_handle);
    }
    else {
        log_recovered(read_handle);
    }
}

void log_imp::log(const log_entry_handle& handle) {
    auto category_idx = handle.get_log_head().category_idx;
    if (categories_mask_array_.size() <= category_idx ||
        categories_mask_array_[category_idx] == 0) {
        return;
    }
    for (decltype(appenders_list_)::size_type i = 0; i < appenders_list_.size(); i++) {
        appenders_list_[i]->log(handle);
    }
    if (snapshot_->is_enable()) {
        snapshot_->write_data(handle);
    }
}

void log_imp::log_recovered(const log_entry_handle& handle) {
    auto category_idx = handle.get_log_head().category_idx;
    if (categories_mask_array_.size() <= category_idx ||
        categories_mask_array_[category_idx] == 0) {
        return;
    }
    for (decltype(recovery_appenders_)::size_type i = 0; i < recovery_appenders_.size(); ++i) {
        recovery_appenders_[i]->log(handle);
    }
    if (snapshot_->is_enable()) {
        snapshot_->write_data(handle);
    }
}

const std::string& log_imp::take_snapshot_string(const std::string& time_zone_config) {
    return snapshot_->take_snapshot_string(time_zone_config);
}

void log_imp::release_snapshot_string() {
    snapshot_->release_snapshot_string();
}

const std::string& log_imp::get_name() const {
    return name_;
}

void log_imp::refresh_merged_log_level_bitmap() {
    log_level_bitmap tmp;
    uint32_t& bitmap_value = *tmp.get_bitmap_ptr();
    for (decltype(appenders_list_)::size_type i = 0; i < appenders_list_.size(); ++i) {
        uint32_t bitmap = *(appenders_list_[i]->get_log_level_bitmap().get_bitmap_ptr());
        bitmap_value |= bitmap;
    }
    // make sure atomic
    merged_log_level_bitmap_ = tmp;
}

bool log_imp::process(bool is_force_flush) {
    constexpr uint64_t flush_io_min_interval_ms = 100;
    uint64_t current_epoch_ms = 0;
    bool did_work = false;
    while (true) {
        auto read_chunk = buffer_->read_chunk();
        scoped_log_buffer_handle<qlog::buffer::log_buffer> scoped_read_chunk(*buffer_, read_chunk);
        if (read_chunk.result == enum_buffer_result_code::success) {
            did_work = true;
            qlog::runtime::log_entry_handle log_item(read_chunk.data_addr, read_chunk.data_size);
            current_epoch_ms = log_item.get_log_head().timestamp_epoch;
            process_log_chunk(log_item);
        } else if (read_chunk.result == enum_buffer_result_code::err_empty_log_buffer) {
            break;
        }
    }
    if (0 == current_epoch_ms) {
        current_epoch_ms = qlog::platform::high_performance_epoch_ms();
    }
    if (is_force_flush) {
        flush_appenders_cache();
        last_flush_io_epoch_ms_ = current_epoch_ms;
    } else {
        if (current_epoch_ms > last_flush_io_epoch_ms_ + flush_io_min_interval_ms) {
            flush_appenders_cache();
            last_flush_io_epoch_ms_ = current_epoch_ms;
        }
    }
    return did_work;
}

void log_imp::sync_process(bool is_force_flush) {
    qlog::platform::scoped_spin_lock lock(spin_lock_);
    uint64_t current_epoch_ms = 0;
    if (!sync_buffer_.get().is_empty()) {
        qlog::runtime::log_entry_handle log_item(sync_buffer_.get().get_aligned_data(),
                                                 sync_buffer_.get().get_used_data_size());
        current_epoch_ms = log_item.get_log_head().timestamp_epoch;
        process_log_chunk(log_item);
        sync_buffer_.get().recycle_data();
    } else {
        current_epoch_ms = qlog::platform::high_performance_epoch_ms();
    }
    constexpr uint64_t flush_io_min_interval_ms = 100;

    if (is_force_flush) {
        flush_appenders_cache();
        last_flush_io_epoch_ms_ = current_epoch_ms;
    } else {
        if (current_epoch_ms > last_flush_io_epoch_ms_ + flush_io_min_interval_ms) {
            flush_appenders_cache();
            last_flush_io_epoch_ms_ = current_epoch_ms;
        }
    }
}

uint8_t* log_imp::get_sync_buffer(uint32_t data_size) {
    return sync_buffer_.get().alloc_data(data_size);
}

const qlog::runtime::layout& log_imp::get_layout() const {
    return layout_;
}

void log_imp::flush_appenders_cache() {
    for (decltype(appenders_list_)::size_type i = 0; i < appenders_list_.size(); ++i) {
        switch (appenders_list_[i]->get_type()) {
            case appender_base::appender_type::raw_file:
            case appender_base::appender_type::text_file:
            case appender_base::appender_type::compressed_file:
                static_cast<qlog::runtime::appender_file_base*>(appenders_list_[i].operator->())
                    ->flush_write_cache();
                break;
            default:
                break;
        }
    }
}

void log_imp::flush_appenders_io() {
    for (decltype(appenders_list_)::size_type i = 0; i < appenders_list_.size(); ++i) {
        switch (appenders_list_[i]->get_type()) {
            case appender_base::appender_type::raw_file:
            case appender_base::appender_type::text_file:
            case appender_base::appender_type::compressed_file:
                static_cast<qlog::runtime::appender_file_base*>(appenders_list_[i].operator->())
                    ->flush_write_io();
                break;
            default:
                break;
        }
    }
}

uint32_t log_imp::get_categories_count() const {
    return (uint32_t)categories_name_array_.size();
}

const std::string& log_imp::get_category_name_by_index(uint32_t index) const {
    return categories_name_array_[index];
}

const std::vector<std::string>& log_imp::get_categories_name() const {
    return categories_name_array_;
}

void log_imp::set_appender_enable(const std::string& appender_name, bool enable) {
    qlog::platform::scoped_spin_lock lock(spin_lock_);
    auto star_list = qlog::utility::split_nonempty(appender_name, '*');
    bool vague = (appender_name.find("*") != string::npos);
    for (auto iter = appenders_list_.begin(); iter != appenders_list_.end(); ++iter) {
        if (vague) {
            bool find = true;
            string temp_cell = (*iter)->get_name();
            for (auto& star : star_list) {
                size_t index = temp_cell.find(star);
                if (index == string::npos) {
                    find = false;
                    break;
                } else {
                    size_t end_pos = index + star.size();
                    if (end_pos < temp_cell.size())
                        temp_cell = temp_cell.substr(end_pos, temp_cell.size() - end_pos);
                    else
                        temp_cell = "";
                }
            }
            if (find) {
                (*iter)->set_enable(enable);
            }
        } else if ((*iter)->get_name() == appender_name) {
            (*iter)->set_enable(enable);
        }
    }
}

}  // namespace qlog::runtime
