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
#pragma once
#include "qlog/buffer/log_buffer.hpp"
#include "qlog/runtime/appender/appender_base.hpp"
#include "qlog/runtime/log_level_bitmap.hpp"
#include "qlog/runtime/log_types.hpp"
#include "qlog/runtime/log_worker.hpp"

namespace qlog::runtime {
constexpr uint64_t log_id_magic_number = 0x24FE284C23EA5821;
class log_imp {
    friend class log_manager;
    friend class qlog::log;

   public:
    log_imp();
    ~log_imp();
    bool init(const std::string& name, const property_value& config,
              const std::vector<std::string>& category_names);
    bool reset_config(const property_value& config);
    void log(const log_entry_handle& handle);

    const std::string& take_snapshot_string(const std::string& time_zone_config);
    void release_snapshot_string();

    const std::string& get_name() const;
    uint32_t get_categories_count() const;
    const std::string& get_category_name_by_index(uint32_t index) const;
    const std::vector<std::string>& get_categories_name() const;

    void set_appender_enable(const std::string& appender_name, bool enable);

    qlog_forceinline qlog::buffer::log_buffer& get_buffer() {
        if (buffer_) {
            return *buffer_;
        }
        assert(false && "null log buffer");
        return *reinterpret_cast<qlog::buffer::log_buffer*>(this);
    }

    qlog_forceinline const qlog::buffer::log_buffer& get_buffer() const {
        if (buffer_) {
            return *buffer_;
        }
        assert(false && "null log buffer");
        return *reinterpret_cast<const qlog::buffer::log_buffer*>(this);
    }

    qlog_forceinline uint64_t id() const {
        return id_;
    }

    qlog_forceinline log_thread_mode get_thread_mode() const {
        return thread_mode_;
    }

    qlog_forceinline log_worker& get_worker() {
        return worker_;
    }

    void set_config(const std::string& config);

    std::string& get_config();

    bool process(bool is_force_flush);

    void sync_process(bool is_force_flush);

    uint8_t* get_sync_buffer(uint32_t data_size);

    const layout& get_layout() const;

    qlog_forceinline bool is_enable_for(uint32_t category_index, qlog::LogLevel level) const {
        return merged_log_level_bitmap_.have_level(level) && categories_mask_array_[category_index];
    }

    qlog_forceinline bool is_stack_trace_enable_for(qlog::LogLevel level) const {
        return print_stack_level_bitmap_.have_level(level);
    }

   private:
    bool add_appender(const string& name, const qlog::runtime::property_value& jobj);
    void refresh_merged_log_level_bitmap();
    void flush_appenders_cache();
    void flush_appenders_io();
    void clear();
    void process_log_chunk(qlog::runtime::log_entry_handle& read_handle);
    void log_recovered(const log_entry_handle& handle);

   private:
    enum class recover_status_enum {
        not_started,    // not started recovery or no recovery data found
        in_recovering,  // in recovering
        recovered,      // recovered finished
    };

   private:
    uint64_t id_;
    log_thread_mode thread_mode_;
    log_worker worker_;
    layout layout_;
    std::string name_;
    qlog::platform::spin_lock spin_lock_;
    log_level_bitmap merged_log_level_bitmap_;
    log_level_bitmap print_stack_level_bitmap_;
    qlog::buffer::log_buffer* buffer_;
    class log_snapshot* snapshot_;
    uint64_t last_log_entry_epoch_ms_;
    uint64_t last_flush_io_epoch_ms_;
    recover_status_enum recover_status_;
    std::vector<std::unique_ptr<appender_base>> appenders_list_;
    std::vector<appender_base*> recovery_appenders_;
    bool recovery_appenders_need_begin_;
    std::vector<std::string> categories_name_array_;
    std::vector<uint8_t> categories_mask_array_;

    std::string last_config_;
};
}  // namespace qlog::runtime
