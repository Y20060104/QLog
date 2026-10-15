#pragma once

#include <cstdint>

#include "qlog/global/common_vars.hpp"
#include "qlog/global/global_vars.hpp"
#include "qlog/platform/atomic/atomic.hpp"
#include "qlog/runtime/appender/appender_console.hpp"
#include "qlog/runtime/log_manager.hpp"

namespace qlog {
// BQLog V1 runtime globals; Decoder/foreign-language globals are outside this module.
struct log_global_vars : public global_vars_base<log_global_vars, common_global_vars> {
    friend struct scoped_thread_check_disable;
    const char* log_appender_type_names_[4] = {"console", "text_file", "raw_file",
                                               "compressed_file"};
    const char* log_recover_start_str_ =
        "/************************************* BQLOG RECOVER START "
        "*************************************/";
    const char* log_recover_end_str_ =
        "/************************************* BQLOG RECOVER END "
        "*************************************/";
    qlog::runtime::appender_console::console_static_misc console_static_misc_;
    qlog::runtime::log_manager log_manager_inst_;

   private:
#if defined(QLOG_DEBUG)
    qlog::platform::atomic<std::int32_t> thread_check_enable_counter_ = 0;
#endif

   protected:
    virtual void partial_destruct() override;

   public:
    log_global_vars();

    qlog_forceinline bool is_thread_check_enabled() const {
#if defined(QLOG_DEBUG)
        return thread_check_enable_counter_.load_relaxed() == 0;
#else
        return false;
#endif
    }
};

struct scoped_thread_check_disable {
    scoped_thread_check_disable() {
#if defined(QLOG_DEBUG)
        qlog::log_global_vars::get().thread_check_enable_counter_.fetch_add_relaxed(1);
#endif
    }

    ~scoped_thread_check_disable() {
#if defined(QLOG_DEBUG)
        qlog::log_global_vars::get().thread_check_enable_counter_.fetch_sub_relaxed(1);
#endif
    }
};
}  // namespace qlog
