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
#include "qlog/runtime/appender/appender_base.hpp"
#include "qlog/runtime/log_imp.hpp"
#include "qlog/runtime/log_types.hpp"
#include "qlog/runtime/log_worker.hpp"

namespace qlog::runtime {
class log_manager {
   private:
    enum class phase { invalid, working, uninitialized };
    friend struct qlog::log_global_vars;

   private:
    log_manager();

   public:
    ~log_manager();
    static log_manager& instance();

   public:
    uint64_t create_log(std::string log_name, const std::string& config_content,
                        const std::vector<std::string>& category_names);

    bool reset_config(const std::string& log_name, const std::string& config_content);

    bool process_by_worker(log_imp* target_log, bool is_force_flush);

    uint32_t get_logs_count() const;

    log_imp* get_log_by_index(uint32_t index);

    qlog_forceinline static log_imp* get_log_by_id(uint64_t log_id) {
        if (!log_id) {
            return nullptr;
        }
        log_id ^= log_id_magic_number;
        return reinterpret_cast<log_imp*>((uintptr_t)(log_id));
    }

    void force_flush_all();

    bool try_flush_all();

    void force_flush(uint64_t log_id);

    qlog_forceinline log_worker& get_public_worker() {
        return public_worker_;
    }

    void uninit();

    /// <summary>
    /// get the public layout which can only be used in public_worker
    /// </summary>
    /// <returns></returns>
    qlog::runtime::layout& get_public_layout();

    void try_restart_worker(log_worker* worker_ptr);

   private:
    qlog::platform::atomic<phase> phase_;
    std::vector<std::unique_ptr<log_imp>> log_imp_list_;
    qlog::runtime::log_worker public_worker_;
    qlog::runtime::layout public_layout_;
    qlog::platform::spin_lock_rw_crazy logs_lock_;
    qlog::platform::spin_lock uninit_lock_;
    qlog::platform::atomic<int32_t> automatic_log_name_seq_;
};
}  // namespace qlog::runtime
