#include "qlog/layout/time_zone.hpp"
#if defined(EXPECT_DEBUG) && !defined(QLOG_DEBUG)
#error Debug consumer must inherit QLOG_DEBUG
#endif
#if !defined(EXPECT_DEBUG) && defined(QLOG_DEBUG)
#error Non-Debug consumer must not inherit QLOG_DEBUG
#endif
int main(int argc, char**) {
    using qlog::layout::TimeZone;
    TimeZone zone("UTC+8");
    TimeZone restored(false, 8, 0, 28800000, "UTC+8");
    restored.reset();
    restored.parse_by_string("localtime");
    restored.restore_by_config(false, 8, 0, 28800000, "UTC+8");
    const auto epoch = static_cast<std::uint64_t>(argc);
    std::tm calendar{};
    const bool converted = zone.get_tm_by_epoch(epoch, calendar);
    zone.refresh_time_string_cache(epoch);
    const auto text = zone.get_time_str_by_epoch(epoch);
    const auto name = TimeZone::get_local_timezone_name();
    return converted && !text.empty() && !name.empty()
        && !zone.is_use_local_time()
        && zone.get_gmt_offset_hours() == 8
        && zone.get_gmt_offset_minutes() == 0
        && zone.get_gmt_offset_to_epoch_ms() == 28800000
        && zone.get_time_zone_diff_to_gmt_ms() == 28800000
        && !zone.get_time_zone_str().empty()
        && zone.get_time_string_cache() != nullptr
        && zone.get_time_string_cache_len() != 0 ? 0 : 1;
}
