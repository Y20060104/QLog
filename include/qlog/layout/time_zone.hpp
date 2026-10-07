#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>

namespace qlog::layout {
class TimeZone {
   public:
    static constexpr std::uint32_t MAX_TIME_SIZE_LEN = 128;

    TimeZone(const std::string& time_zone_str = "localtime");

    TimeZone(bool use_local_time, std::int32_t gmt_offset_hours, std::int32_t gmt_offset_minutes,
             std::int32_t time_zone_diff_to_gmt_ms, const std::string& time_zone_str);

    ~TimeZone();

    void reset();

    void parse_by_string(const std::string& time_zone_str);

    void restore_by_config(bool use_local_time, std::int32_t gmt_offset_hours,
                           std::int32_t gmt_offset_minutes, std::int32_t time_zone_diff_to_gmt_ms,
                           const std::string& time_zone_str);

    bool get_tm_by_epoch(std::uint64_t epoch_ms, std::tm& result) const;  // CR:tm 是什么类型？

    std::string get_time_str_by_epoch(std::uint64_t epoch_ms) const;

    static std::string get_local_timezone_name();

    void refresh_time_string_cache(std::uint64_t epoch_ms);

    bool is_use_local_time() const {
        return use_local_time_;
    }

    std::int32_t get_gmt_offset_hours() const {
        return gmt_offset_hours_;
    }

    std::int32_t get_gmt_offset_minutes() const {
        return gmt_offset_minutes_;
    }

    std::int32_t get_gmt_offset_to_epoch_ms() const {
        return (gmt_offset_hours_ * 3600 + gmt_offset_minutes_ * 60) * 1000;
    }

    const std::string& get_time_zone_str() const {
        return time_zone_str_;
    }

    std::int32_t get_time_zone_diff_to_gmt_ms() const {
        return time_zone_diff_to_gmt_ms_;
    }

    const char* get_time_string_cache() const {
        return time_cache_;
    }

    std::size_t get_time_string_cache_len() const {
        return time_cache_len_;
    }

   private:
    void inner_refresh_time_string_cache(std::uint64_t epoch_ms);

    bool use_local_time_ = true;
    std::int32_t gmt_offset_hours_ = 0;
    std::int32_t gmt_offset_minutes_ = 0;
    std::int32_t time_zone_diff_to_gmt_ms_ = 0;
    std::string time_zone_str_;

    char time_cache_[MAX_TIME_SIZE_LEN + 1] = {};
    std::size_t time_cache_len_ = 0;
    std::uint64_t last_time_epoch_cache_ = 0;
};
}  // namespace qlog::layout