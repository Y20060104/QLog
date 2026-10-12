#include "qlog/utility/diagnostics.hpp"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "qlog/global/common_vars.hpp"
#include "qlog/platform/mutex.hpp"

namespace qlog::utility {

static util::type_func_ptr_qlog_util_console_callback console_callback_ = nullptr;

#if defined(QLOG_TOOLS) || defined(QLOG_UNIT_TEST)
static qlog::LogLevel log_device_min_level = qlog::LogLevel::warning;

void util::set_log_device_console_min_level(qlog::LogLevel level) {
    log_device_min_level = level;
}
#endif

void util::log_device_console(qlog::LogLevel level, const char* format, ...) {
#if defined(QLOG_TOOLS) || defined(QLOG_UNIT_TEST)
    if (level < log_device_min_level) {
        return;
    }
#endif

    auto& device_console_buffer = qlog::common_global_vars::get().device_console_buffer_;
    qlog::platform::scoped_mutex lock(qlog::common_global_vars::get().console_mutex_);

    if (format) {
        while (true) {
            std::va_list args;  // cR:这个是什么
            va_start(args, format);

            bool failed =
                (static_cast<decltype(device_console_buffer.size())>(std::vsnprintf(
                     device_console_buffer.data(), device_console_buffer.size(), format, args)) +
                 1) >= device_console_buffer.size();

            va_end(args);  // CR:这个是？

            if (failed) {
                device_console_buffer.resize(device_console_buffer.size() *
                                             2);  // CR为什么不用roundpower2？
            } else {
                break;
            }
        }
    } else {
        device_console_buffer[0] = '\0';
    }

    log_device_console_plain_text(level, device_console_buffer.data());
}

void util::log_device_console_plain_text(qlog::LogLevel level, const char* text) {
    auto callback = console_callback_;
    if (callback) {
        callback(level, text);  // CR:callback函数有什么用？
    } else {
        _default_console_output(level, text);
    }
}

void util::set_console_output_callback(type_func_ptr_qlog_util_console_callback callback) {
    console_callback_ = callback;
}

void util::_default_console_output(qlog::LogLevel level, const char* text) {
#if defined(QLOG_TOOLS) || defined(QLOG_UNIT_TEST)
    if (level < log_device_min_level) {
        return;
    }
#endif

#if defined(QLOG_IN_GITHUB_ACTIONS)
    switch (level) {
        case qlog::LogLevel::verbose:
            std::printf("[QLog][V] %s\n", text ? text : "");
            break;

        case qlog::LogLevel::debug:
            std::printf("[QLog][D] %s\n", text ? text : "");
            break;

        case qlog::LogLevel::info:
            std::printf("[QLog][I] %s\n", text ? text : "");
            break;
        case qlog::LogLevel::warning:
            std::printf("[QLog][W] %s\n", text ? text : "");
            break;
        case qlog::LogLevel::error:
            std::printf("[QLog][E] %s\n", text ? text : "");
            break;
        case qlog::LogLevel::fatal:
            std::printf("[QLog][F] %s\n", text ? text : "");
            break;
        default:
            break;
    }
#else

    qlog::platform::scoped_mutex lock(qlog::common_global_vars::get().console_mutex_);

    auto color = "";
    switch (level) {
        case qlog::LogLevel::verbose:
            color = "\x1b[3m";
            break;

        case qlog::LogLevel::debug:
            color = "\x1b[92m";
            break;
        case qlog::LogLevel::info:
            color = "\x1b[94m";
            break;
        case qlog::LogLevel::warning:
            color = "\x1b[1;40;93m";
            break;
        case qlog::LogLevel::error:
            color = "\x1b[1;40;91m";
            break;
        case qlog::LogLevel::fatal:
            color = "\x1b[1;30;101m";
            break;
        default:
            color = "\x1b[37m";
            break;
    }  // cR:\x1b是什么 [后又是什么

    auto reset = "\x1b[0m";
    auto prefix = "[QLog]";
    auto newline = "\n";
    const char* msg = text ? text : "";

    const std::size_t color_len = std::strlen(color);
    const std::size_t reset_len = std::strlen(reset);
    const std::size_t prefix_len = std::strlen(prefix);
    const std::size_t msg_len = std::strlen(msg);
    constexpr std::size_t nl_len = 1;

    bool to_stderr = level == qlog::LogLevel::error || level == qlog::LogLevel::fatal;

    constexpr std::size_t STACK_CAP = 1024;

#if defined(_WIN32)
    HANDLE h = ::GetStdHandle(to_stderr ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);

    if (h && h != INVALID_HANDLE_VALUE) {
        static bool s_vt_inited = false;
        static bool s_vt_enabled = false;
        DWORD mode = 0;
        const bool is_console = ::GetConsoleMode(h, &mode);

        if (is_console && !s_vt_inited) {
            s_vt_enabled = ::SetConsoleMode(h, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != 0;
            s_vt_inited = true;
        }

        const bool use_color = is_console && s_vt_enabled;
        const std::size_t total_len = (use_color ? color_len : 0) + prefix_len + msg_len +
                                      (use_color ? reset_len : 0) + nl_len;  // CR:reset_len是什么？

        if (total_len <= STACK_CAP) {
            char buf[STACK_CAP];
            char* p = buf;

            if (use_color) {
                std::memcpy(p, color, color_len);
                p += color_len;
            }
            std::memcpy(p, prefix, prefix_len);
            p += prefix_len;
            if (msg_len) {
                std::memcpy(p, msg, msg_len);
                p += msg_len;
            }
            if (use_color) {
                std::memcpy(p, reset, reset_len);
                p += reset_len;
            }
            std::memcpy(p, newline, nl_len);
            p += nl_len;

            DWORD written = 0;
            WriteFile(h, buf, static_cast<DWORD>(p - buf), &written, nullptr);
        } else {
            DWORD written = 0;
            if (use_color && color_len) {
                WriteFile(h, color, static_cast<DWORD>(color_len), &written, nullptr);
            }
            WriteFile(h, prefix, static_cast<DWORD>(prefix_len), &written, nullptr);
            if (msg_len) {
                WriteFile(h, msg, static_cast<DWORD>(msg_len), &written, nullptr);
            }
            if (use_color && reset_len) {
                WriteFile(h, reset, static_cast<DWORD>(reset_len), &written, nullptr);
            }
            WriteFile(h, newline, static_cast<DWORD>(nl_len), &written, nullptr);
        }
    }
#if defined(_MSC_VER) && !defined(__clang__)
    OutputDebugStringA(prefix);
    OutputDebugStringA(msg);
    OutputDebugStringA("\n");
#endif

#else
    const std::int32_t fd = to_stderr ? STDERR_FILENO : STDOUT_FILENO;
    const bool is_tty = isatty(fd) == 1;

    const bool use_color = is_tty;
    const std::size_t total_len =
        (use_color ? color_len : 0) + prefix_len + msg_len + (use_color ? reset_len : 0) + nl_len;

    ssize_t written = 0;
    if (total_len <= STACK_CAP) {
        char buf[STACK_CAP];
        char* p = buf;

        if (use_color) {
            std::memcpy(p, color, color_len);
            p += color_len;
        }
        std::memcpy(p, prefix, prefix_len);
        p += prefix_len;
        if (msg_len) {
            std::memcpy(p, msg, msg_len);
            p += msg_len;
        }
        if (use_color) {
            std::memcpy(p, reset, reset_len);
            p += reset_len;
        }
        std::memcpy(p, newline, nl_len);
        p += nl_len;

        written += write(fd, buf, static_cast<std::size_t>(p - buf));
    } else {
        if (use_color && color_len) {
            written += write(fd, color, color_len);
        }
        written += write(fd, prefix, prefix_len);
        if (msg_len) {
            written += write(fd, msg, msg_len);
        }
        if (use_color && reset_len) {
            written += write(fd, reset, reset_len);
        }
        written += write(fd, newline, nl_len);
    }
    (void)written;
#endif
#endif
}

}  // namespace qlog::utility
