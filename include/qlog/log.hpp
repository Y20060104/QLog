#pragma once
#include "qlog/platform/stack_trace.hpp"
#include "qlog/record/argument_serialization.hpp"
#include "qlog/runtime/appender/appender_console.hpp"
#include "qlog/runtime/log_imp.hpp"
#include "qlog/runtime/log_manager.hpp"
#include "qlog/runtime/record_writer.hpp"
namespace qlog {
// Non-owning public log handle; Manager owns the corresponding runtime object.
class log {
   public:
    log() = default;
    static log create_log(const std::string& name, const std::string& config_content,
                          const std::vector<std::string>& category_names = {""});
    static log get_log_by_name(const std::string& name);
    static bool reset_config(const std::string& name, const std::string& config_content);
    static void force_flush_all();
    static void uninit();
    static void register_console_callback(runtime::type_func_ptr_console_callback callback);
    static void unregister_console_callback(runtime::type_func_ptr_console_callback callback);
    static void set_console_buffer_enable(bool enable);
    static bool fetch_and_remove_console_buffer(
        runtime::type_func_ptr_console_buffer_fetch_callback callback,
        const void* pass_through_param = nullptr);
    bool is_valid() const {
        return impl_ != nullptr;
    }
    std::uint64_t get_id() const {
        return impl_ ? impl_->id() : 0;
    }
    const std::string& get_name() const;
    const std::vector<std::string>& get_categories_name() const;
    runtime::log_thread_mode get_thread_mode() const;
    bool is_enable_for(std::uint32_t category_index, LogLevel level) const;
    void set_appender_enable(const std::string& appender_name, bool enable) const;
    void force_flush() const;
    std::string take_snapshot(const std::string& time_zone_config = "localtime") const;
    template <typename STR, typename... Args>
    bool do_log(std::uint32_t category_index, LogLevel level, const STR& log_format_content,
                const Args&... args) const {
        constexpr auto tag = record::detail::get_log_param_type_enum<STR>();
        static_assert(tag == record::ArgumentTag::string_utf8_type ||
                          tag == record::ArgumentTag::string_utf16_type,
                      "log format must be a supported string");
        if (!is_enable_for(category_index, level)) {
            return false;
        }
        bool should_print_stack = impl_->is_stack_trace_enable_for(level);
        const char* stack_data = nullptr;
        std::uint32_t stack_bytes = 0;
        if (should_print_stack) {
            if constexpr (tag == record::ArgumentTag::string_utf8_type) {
                platform::get_stack_trace(0, stack_data, stack_bytes);
            } else {
                const char16_t* stack_u16 = nullptr;
                std::uint32_t stack_units = 0;
                platform::get_stack_trace_utf16(0, stack_u16, stack_units);
                stack_data = reinterpret_cast<const char*>(stack_u16);
                stack_bytes = stack_units << 1;
            }
        }
        std::size_t format_size =
            record::detail::StringStorage<STR>::get_storage_data_size(log_format_content);
        std::size_t total_format_data_size = format_size + stack_bytes;
        auto args_size_seq = record::detail::make_size_seq<true>(args...);
        const void* format_data_ptr = nullptr;
        constexpr auto kind = record::detail::get_serialize_kind<STR>();
        if constexpr (kind == record::detail::SerializeKind::utf8_string ||
                      kind == record::detail::SerializeKind::utf16_string) {
            if (!should_print_stack) {
                format_data_ptr = record::detail::get_string_source(log_format_content).data;
            }
        }
        auto handle = runtime::log_write_begin(
            get_id(), static_cast<std::uint8_t>(level), category_index,
            static_cast<std::uint8_t>(tag), static_cast<std::uint32_t>(total_format_data_size),
            format_data_ptr, static_cast<std::uint32_t>(args_size_seq.get_total()));
        if (handle.result != buffer::enum_buffer_result_code::success) {
            return false;
        }
        if (!format_data_ptr) {
            record::detail::type_copy<false>(log_format_content,
                                             handle.format_data_addr - sizeof(std::uint32_t),
                                             format_size + sizeof(std::uint32_t));
            if (stack_bytes) {
                std::memcpy(handle.format_data_addr + format_size, stack_data, stack_bytes);
            }
            *reinterpret_cast<std::uint32_t*>(handle.format_data_addr - sizeof(std::uint32_t)) =
                static_cast<std::uint32_t>(total_format_data_size);
        }
        record::detail::fill_arguments(
            handle.format_data_addr + utility::align_4(total_format_data_size), args_size_seq,
            args...);
        runtime::log_write_finish(get_id(), handle);
        return true;
    }
    template <typename STR, typename... Args>
    bool verbose(const STR& format, const Args&... args) const {
        return do_log(0, LogLevel::verbose, format, args...);
    }
    template <typename STR, typename... Args>
    bool debug(const STR& format, const Args&... args) const {
        return do_log(0, LogLevel::debug, format, args...);
    }
    template <typename STR, typename... Args>
    bool info(const STR& format, const Args&... args) const {
        return do_log(0, LogLevel::info, format, args...);
    }
    template <typename STR, typename... Args>
    bool warning(const STR& format, const Args&... args) const {
        return do_log(0, LogLevel::warning, format, args...);
    }
    template <typename STR, typename... Args>
    bool error(const STR& format, const Args&... args) const {
        return do_log(0, LogLevel::error, format, args...);
    }
    template <typename STR, typename... Args>
    bool fatal(const STR& format, const Args&... args) const {
        return do_log(0, LogLevel::fatal, format, args...);
    }

   private:
    explicit log(runtime::log_imp* impl) : impl_(impl) {}
    runtime::log_imp* impl_ = nullptr;
};
}  // namespace qlog
