#include "qlog/platform/file_path.hpp"

#include <cinttypes>

#include "qlog/global/common_vars.hpp"
#include "qlog/platform/spin_lock.hpp"
#include "qlog/utility/diagnostics.hpp"

namespace qlog::platform {
static qlog::platform::spin_lock_zero_init lock_;

std::string get_base_dir(std::int32_t base_dir_type) {
    qlog::platform::scoped_spin_lock lock(lock_);  // CR:为什么没有对象 直接调用了 lock函数？

    if (base_dir_type == 0) {
        return qlog::common_global_vars::get().base_dir_init_inst_.get_base_dir_0();
    } else if (base_dir_type == 1) {
        return qlog::common_global_vars::get().base_dir_init_inst_.get_base_dir_1();
    } else {
        qlog::utility::util::log_device_console(qlog::LogLevel::warning,
                                                "[get_base_dir] unknown base dir type:%" PRId32,
                                                base_dir_type);

        return qlog::common_global_vars::get().base_dir_init_inst_.get_base_dir_1();
    }
}

void base_dir_initializer::set_base_dir_0(const std::string& dir) {
    qlog::platform::scoped_spin_lock lock(lock_);
    base_dir_0_ = dir;

#if !defined(NDEBUG)
    qlog::utility::util::log_device_console(qlog::LogLevel::info, "set base dir type 0: %s",
                                            dir.c_str());
#endif
}

void base_dir_initializer::set_base_dir_1(const std::string& dir) {
    qlog::platform::scoped_spin_lock lock(lock_);
    base_dir_1_ = dir;

#if !defined(NDEBUG)
    qlog::utility::util::log_device_console(qlog::LogLevel::info, "set base dir type 1: %s",
                                            dir.c_str());
#endif
}

}  // namespace qlog::platform