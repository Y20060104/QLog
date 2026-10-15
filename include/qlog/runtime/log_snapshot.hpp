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

// this class is not thread safe.
// write_data() and take_snapshot() can not be called in the same time
#include "qlog/buffer/siso_ring_buffer.hpp"
#include "qlog/layout/layout.hpp"
#include "qlog/runtime/log_level_bitmap.hpp"
#include "qlog/runtime/log_types.hpp"

namespace qlog::runtime {
class log_snapshot {
   public:
    log_snapshot(class log_imp* parent_log, const qlog::runtime::property_value& snapshot_config);

    log_snapshot(const log_snapshot& rhs) = delete;

    ~log_snapshot();

    void reset_config(const qlog::runtime::property_value& snapshot_config);

    void write_data(const qlog::runtime::log_entry_handle& log_entry);

    const std::string& take_snapshot_string(const std::string& time_zone_config);

    // take_snapshot_string and release_snapshot_string must be called in pair, or the lock will not
    // be released
    void release_snapshot_string();

    qlog_forceinline bool is_enable() const {
        return (bool)snapshot_buffer_;
    }

   private:
    uint32_t buffer_size_;
    siso_ring_buffer* snapshot_buffer_;
    uint8_t* buffer_data_;
    std::string snapshot_text_[2];
    uint32_t snapshot_text_index_;
    bool snapshot_text_continuous_;

    qlog::platform::spin_lock lock_;

    log_level_bitmap log_level_bitmap_;
    std::vector<uint8_t> categories_mask_array_;

    layout snapshot_layout_;
    class log_imp* parent_log_;
};
}  // namespace qlog::runtime
