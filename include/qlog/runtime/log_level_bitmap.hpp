#pragma once

#include <cassert>
#include <cstdint>
#include <string>

#include "qlog/log_level.hpp"

namespace qlog::runtime {

class LogLevelBitmap {
   public:
    LogLevelBitmap();
    LogLevelBitmap(std::uint32_t init_bitmap_value);
    LogLevelBitmap(const LogLevelBitmap& rhs);

    LogLevelBitmap& operator=(const LogLevelBitmap& rhs);

    void clear(){
        bitmap_=0;
    }

    bool have_level(qlog::LogLevel level) const {
        const auto index = static_cast<std::int32_t>(level);
        assert(index >= 0 && index < 32);

        return (bitmap_ & (std::uint32_t{1} << static_cast<std::uint32_t>(index))) != 0;
    }

    void add_level(qlog::LogLevel level);
    void add_level(const std::string& level_string);
    void del_level(qlog::LogLevel level);

    std::uint32_t* get_bitmap_ptr();

   private:
    std::uint32_t bitmap_;
};
}  // namespace qlog::runtime