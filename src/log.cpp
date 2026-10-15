#include "qlog/log.hpp"
namespace qlog {
log log::create_log(const std::string& name, const std::string& config_content,
                    const std::vector<std::string>& category_names) {
    auto categories = category_names;
    if (categories.empty()) {
        categories.push_back("");
    }
    auto id = runtime::log_manager::instance().create_log(name, config_content, categories);
    return log(runtime::log_manager::get_log_by_id(id));
}
log log::get_log_by_name(const std::string& name) {
    auto& manager = runtime::log_manager::instance();
    for (std::uint32_t i = 0; i < manager.get_logs_count(); ++i) {
        auto* impl = manager.get_log_by_index(i);
        if (impl && impl->get_name() == name) {
            return log(impl);
        }
    }
    return {};
}
bool log::reset_config(const std::string& name, const std::string& config_content) {
    return runtime::log_manager::instance().reset_config(name, config_content);
}
void log::force_flush_all() {
    runtime::log_manager::instance().force_flush_all();
}
void log::uninit() {
    runtime::log_manager::instance().uninit();
}
void log::register_console_callback(runtime::type_func_ptr_console_callback callback) {
    runtime::appender_console::register_console_callback(callback);
}
void log::unregister_console_callback(runtime::type_func_ptr_console_callback callback) {
    runtime::appender_console::unregister_console_callback(callback);
}
void log::set_console_buffer_enable(bool enable) {
    runtime::appender_console::set_console_buffer_enable(enable);
}
bool log::fetch_and_remove_console_buffer(
    runtime::type_func_ptr_console_buffer_fetch_callback callback, const void* pass_through_param) {
    return runtime::appender_console::fetch_and_remove_from_console_buffer(callback,
                                                                           pass_through_param);
}
const std::string& log::get_name() const {
    return impl_->get_name();
}
const std::vector<std::string>& log::get_categories_name() const {
    return impl_->get_categories_name();
}
runtime::log_thread_mode log::get_thread_mode() const {
    return impl_->get_thread_mode();
}
bool log::is_enable_for(std::uint32_t category_index, LogLevel level) const {
    return impl_ && impl_->is_enable_for(category_index, level);
}
void log::set_appender_enable(const std::string& appender_name, bool enable) const {
    impl_->set_appender_enable(appender_name, enable);
}
void log::force_flush() const {
    runtime::log_manager::instance().force_flush(get_id());
}
std::string log::take_snapshot(const std::string& time_zone_config) const {
    auto result = impl_->take_snapshot_string(time_zone_config);
    impl_->release_snapshot_string();
    return result;
}
}  // namespace qlog
