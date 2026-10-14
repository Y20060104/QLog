#pragma once

#include <cstdint>

#include "qlog/global/common_vars.hpp"
#include "qlog/global/global_vars.hpp"
#include "qlog/platform/atomic/atomic.hpp"

namespace qlog {
// The original log_global_vars is migrated in stages; this is its Debug dependency.
struct log_global_vars : public global_vars_base<log_global_vars, common_global_vars> {
    friend struct scoped_thread_check_disable;

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
