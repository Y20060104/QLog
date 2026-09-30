#include "qlog/layout/time_zone.hpp"

#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <utility>

namespace qlog::layout {
TimeZone::TimeZone(const std::string& time_zone_str) {
    reset();
    parse_by_string(time_zone_str);
}

TimeZone::TimeZone(bool use_local_time, std::int32_t gmt_offset_hours,
                   std::int32_t gmt_offset_minutes, std::int32_t time_zone_diff_to_gmt_ms,
                   const std::string& time_zone_str) {
    restore_by_config(use_local_time, gmt_offset_hours, gmt_offset_minutes,
                      time_zone_diff_to_gmt_ms, time_zone_str);
}

TimeZone::~TimeZone() = default;

void TimeZone::reset() {
    use_local_time_ = true;
    gmt_offset_hours_ = 0;
    gmt_offset_minutes_ = 0;
    time_zone_diff_to_gmt_ms_ = 0;
    time_zone_str_.clear();
    time_cache_[0] = '\0';
    time_cache_len_ = 0;
    last_time_epoch_cache_ = 0;
}

std::string TimeZone::get_local_timezone_name() {
    const std::time_t now = std::time(nullptr);
    std::tm lt{};
    if (::localtime_r(&now, &lt) == nullptr) {
        std::fprintf(stderr, "failed to fetch localtime.\n");
        return "localtime";
    }

    char tzbuf[128]{};

    const std::size_t length = std::strftime(tzbuf, sizeof(tzbuf), "%Z", &lt);
    if (length > 0 && tzbuf[0] != '\0') {
        return std::string(tzbuf);
    }
    return "localtime";
}

std::string TimeZone::trim(const std::string& text) {
    std::size_t first = 0;
    std::size_t last = text.size();

    while (first < last && std::isspace(static_cast<unsigned char>(text[first])) != 0) {
        ++first;
    }

    while (first < last && std::isspace(static_cast<unsigned char>(text[last - 1])) != 0) {
        --last;
    }
    return text.substr(first, last - first);
}

void TimeZone::parse_by_string(const std::string& time_zone_str) {
    reset();

    std::string upper_time_zone = trim(time_zone_str);
    for (char& c : upper_time_zone) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    const auto warn_invalid = [&time_zone_str]() {
        std::fprintf(stderr, "invalid time_zone string %s, use local time as default.\n",
                     time_zone_str.c_str());
    };

    do {
        if (upper_time_zone.empty()) {
            std::fprintf(stderr, "empty time_zone string, use local time as default.\n");
            break;
        }

        if (upper_time_zone == "LOCAL" || upper_time_zone == "LOCALTIME" ||
            upper_time_zone == "LOCAL_TIME") {
            break;
        }

        if (upper_time_zone == "GMT" || upper_time_zone == "Z" || upper_time_zone == "UTC") {
            use_local_time_ = false;
            break;
        }

        const std::size_t parse_start = upper_time_zone.starts_with("UTC") ? 3U : 0U;

        const char* start = upper_time_zone.c_str() + parse_start;
        char* end = nullptr;

        errno = 0;
        const long parsed_hour = std::strtol(start, &end, 10);
        if (end == start || errno == ERANGE || parsed_hour < -12 || parsed_hour > 14) {
            warn_invalid();
            break;
        }

        const auto hour = static_cast<std::int32_t>(parsed_hour);
        std::int32_t minute = 0;

        if (*end == ':') {
            start = end + 1;

            errno = 0;
            const long parsed_minute = std::strtol(start, &end, 10);
            if (end == start || errno == ERANGE || parsed_minute < 0 || parsed_minute >= 60) {
                warn_invalid();
                break;
            }

            minute = static_cast<std::int32_t>(parsed_minute);

            gmt_offset_minutes_ = minute;
        }

        if (*end != '\0') {
            warn_invalid();
            break;
        }

        use_local_time_ = false;
        gmt_offset_hours_ = hour;
        gmt_offset_minutes_ = minute;

        if (gmt_offset_hours_ < 0) {
            gmt_offset_minutes_ = -gmt_offset_minutes_;
        }
    } while (false);

    if (use_local_time_) {
        time_zone_str_ = get_local_timezone_name();

        const std::time_t now = std::time(nullptr);
        std::tm lt{};
        std::tm gt{};

        if (::localtime_r(&now, &lt) == nullptr || ::gmtime_r(&now, &gt) == nullptr) {
            return;
        }

        errno = 0;
        const std::time_t local_epoch_sec = std::mktime(&lt);
        if (local_epoch_sec == static_cast<std::time_t>(-1) && errno == EOVERFLOW) {
            return;
        }

        errno = 0;
        const std::time_t utc_epoch_sec = std::mktime(&gt);
        if (utc_epoch_sec == static_cast<std::time_t>(-1) && errno == EOVERFLOW) {
            return;
        }

        const double offset_ms = std::difftime(local_epoch_sec, utc_epoch_sec) * 1000.0;

        if (offset_ms >= static_cast<double>(std::numeric_limits<std::int32_t>::min()) &&
            offset_ms <= static_cast<double>(std::numeric_limits<std::int32_t>::max())) {
            time_zone_diff_to_gmt_ms_ = static_cast<std::int32_t>(offset_ms);
        }
    } else {
        char buffer[64]{};

        if (gmt_offset_hours_ == 0 && gmt_offset_minutes_ == 0) {
            time_zone_str_ = "UTC0";
        } else if (gmt_offset_minutes_ == 0) {
            std::snprintf(buffer, sizeof(buffer), "UTC%+d", static_cast<int>(gmt_offset_hours_));
            time_zone_str_ = buffer;
        } else {
            const int minute = static_cast<int>(gmt_offset_minutes_);
            const int minute_abs = minute < 0 ? -minute : minute;

            std::snprintf(buffer, sizeof(buffer), "UTC%+d:%02d",
                          static_cast<int>(gmt_offset_hours_), minute_abs);
            time_zone_str_ = buffer;
        }

        time_zone_diff_to_gmt_ms_ = (gmt_offset_hours_ * 3600 + gmt_offset_minutes_ * 60) * 1000;
    }
}

bool TimeZone::get_tm_by_epoch(std::uint64_t epoch_ms, std::tm& result) const {
    std::uint64_t epoch_second = epoch_ms / 1000U;

    if (!std::in_range<std::time_t>(epoch_second)) {
        return false;
    }

    const time_t base_sec = static_cast<time_t>(epoch_second);
    if (use_local_time_ == true) return ::localtime_r(&base_sec, &result) != nullptr;

    const std::int64_t delta_sec = static_cast<std::int64_t>(gmt_offset_hours_) * 3600 +
                                   static_cast<std::int64_t>(gmt_offset_minutes_) * 60;

    const std::int64_t adjusted_sec = static_cast<std::int64_t>(epoch_second) + delta_sec;

    if (!std::in_range<std::time_t>(adjusted_sec)) {
        return false;
    }
    const auto adjusted = static_cast<std::time_t>(adjusted_sec);
    return ::gmtime_r(&adjusted, &result) != nullptr;
}

void TimeZone::inner_refresh_time_string_cache(std::uint64_t epoch_ms) {
    std::tm time_st{};
    if (get_tm_by_epoch(epoch_ms, time_st) == false) {
        time_st = {};
    }
    const long long year = static_cast<long long>(time_st.tm_year) + 1900;
    const int written =
        std::snprintf(time_cache_, sizeof(time_cache_), "%s %lld-%02d-%02d %02d:%02d:%02d.",
                      time_zone_str_.c_str(), year, time_st.tm_mon + 1, time_st.tm_mday,
                      time_st.tm_hour, time_st.tm_min, time_st.tm_sec);

    if (written < 0) {
        time_cache_[0] = '\0';
        time_cache_len_ = 0;
    } else {
        const auto required = static_cast<std::size_t>(written);
        const std::size_t capacity = sizeof(time_cache_) - 1;

        time_cache_len_ = required < capacity ? required : capacity;
    }

    last_time_epoch_cache_ = epoch_ms;
}

void TimeZone::refresh_time_string_cache(std::uint64_t epoch_ms) {
    if (epoch_ms == last_time_epoch_cache_) {
        return;
    }

    inner_refresh_time_string_cache(epoch_ms);
}

std::string TimeZone::get_time_str_by_epoch(std::uint64_t epoch_ms) const {
    char time_buffer[128]{};
    std::tm result{};

    if (!get_tm_by_epoch(epoch_ms, result)) {
        result = {};
    }

    const long long year = static_cast<long long>(result.tm_year) + 1900;

    const int written = std::snprintf(time_buffer, sizeof(time_buffer),
                                      "%04lld-%02d-%02d %02d:%02d:%02d", year, result.tm_mon + 1,
                                      result.tm_mday, result.tm_hour, result.tm_min, result.tm_sec);

    if (written < 0) {
        time_buffer[0] = '\0';
    }

    return std::string(time_buffer);
}

void TimeZone::restore_by_config(bool use_local_time, std::int32_t gmt_offset_hours,
                                 std::int32_t gmt_offset_minutes,
                                 std::int32_t time_zone_diff_to_gmt_ms,
                                 const std::string& time_zone_str) {
    use_local_time_ = use_local_time;
    gmt_offset_hours_ = gmt_offset_hours;
    gmt_offset_minutes_ = gmt_offset_minutes;
    time_zone_diff_to_gmt_ms_ = time_zone_diff_to_gmt_ms;
    time_zone_str_ = time_zone_str;
}

}  // namespace qlog::layout