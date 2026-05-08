#include "qlog/appender/appender_file_raw.h"

#include <cstring>

namespace qlog::appender
{
void appender_file_raw::log_impl(const entry_runtime_view& view)
{
    appender_file_base::log_impl(view);
    if (!view.entry_data || view.entry_size == 0)
    {
        return;
    }

    const uint32_t item_size = view.entry_size;
    auto wh = alloc_write_cache(sizeof(item_size) + item_size);
    std::memcpy(wh.data(), &item_size, sizeof(item_size));
    std::memcpy(wh.data() + sizeof(item_size), view.entry_data, item_size);
    return_write_cache(wh);
    mark_write_finished();
}

std::string appender_file_raw::get_file_ext_name() const
{
    return ".lograw";
}
uint32_t appender_file_raw::get_binnary_format_version()
{
    return format_version;
}
} // namespace qlog::appender