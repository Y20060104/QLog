#pragma once

#include "qlog/log_level.hpp"

namespace qlog::utility {

class util {
   public:
#if defined(QLOG_TOOLS) || defined(QLOG_UNIT_TEST)
    static void set_log_device_console_min_level(qlog::LogLevel level);
#endif

    static void log_device_console(qlog::LogLevel level, const char* format, ...)
#if defined(__GNUC__) || defined(__clang__)
        __attribute__((format(printf, 2, 3)))
#endif
        ;
    // CR:讲解代码
    static void log_device_console_plain_text(qlog::LogLevel level, const char* text);

#if defined(_WIN32)
    using type_func_ptr_qlog_util_console_callback = void(__stdcall*)(
        qlog::LogLevel level, const char* text);  // CR:讲解 callback是什么？stdcall呢？
#else
    using type_func_ptr_qlog_util_console_callback = void (*)(qlog::LogLevel level,
                                                              const char* text);
#endif

    static void set_console_output_callback(type_func_ptr_qlog_util_console_callback callback);

    static void _default_console_output(qlog::LogLevel level, const char* text);
};
}  // namespace qlog::utility