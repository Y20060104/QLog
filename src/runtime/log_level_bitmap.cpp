#include "qlog/runtime/log_level_bitmap.hpp"

#include <cctype>
#include <cstdio>
#include <string_view>

namespace qlog::runtime {



LogLevelBitmap::LogLevelBitmap() : bitmap_(0) {}

LogLevelBitmap::LogLevelBitmap(std::uint32_t init_bitmap_value) : bitmap_(init_bitmap_value) {}

LogLevelBitmap::LogLevelBitmap(const LogLevelBitmap& rhs) : bitmap_(rhs.bitmap_) {}

LogLevelBitmap& LogLevelBitmap::operator=(const LogLevelBitmap& rhs) {
    bitmap_ = rhs.bitmap_;
    return *this;
}

void LogLevelBitmap::add_level(qlog::LogLevel level) {
    const auto index = static_cast<std::int32_t>(level);
    assert(index >= 0 && index < 32);

    bitmap_ |= std::uint32_t{1} << static_cast<std::uint32_t>(index);
}

void LogLevelBitmap::add_level(const std::string& level_string) {
    if (equals_ignore_case(level_string, "all")) {
        bitmap_ = 0xFFFFFFFFU;
        return;
    }

    if (equals_ignore_case(level_string, "verbose")) {
        add_level(qlog::LogLevel::verbose);
    } else if (equals_ignore_case(level_string, "debug")) {
        add_level(qlog::LogLevel::debug);
    } else if (equals_ignore_case(level_string, "info")) {
        add_level(qlog::LogLevel::info);
    } else if (equals_ignore_case(level_string, "warning")) {
        add_level(qlog::LogLevel::warning);
    } else if (equals_ignore_case(level_string, "error")) {
        add_level(qlog::LogLevel::error);
    } else if (equals_ignore_case(level_string, "fatal")) {
        add_level(qlog::LogLevel::fatal);
    } else {
        std::fprintf(stderr, "qlog warnning: invalid level mask was found:\"%s\"\n",
                     level_string.c_str());  // CR::\"%s\"\n"如何拆解 这段语法？
    }
}

void LogLevelBitmap::del_level(qlog::LogLevel level) {
    const auto index = static_cast<std::int32_t>(level);
    assert(index >= 0 && index < 32);

    bitmap_ &= ~(std::uint32_t{1} << static_cast<std::uint32_t>(index));
}

std::uint32_t* LogLevelBitmap::get_bitmap_ptr() {
    return &bitmap_;
}
}  // namespace qlog::runtime