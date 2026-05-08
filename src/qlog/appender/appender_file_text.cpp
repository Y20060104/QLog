#include "qlog/appender/appender_file_text.h"

#include <cstring>

namespace qlog::appender
{

namespace
{
constexpr const char* k_recover_start = "/************************************* QLOG RECOVER START "
                                        "*************************************/";
constexpr const char* k_recover_end = "/************************************* QLOG RECOVER END "
                                      "*************************************/";
} // namespace

void appender_file_text::log_impl(const entry_runtime_view& view)
{
    appender_file_base::log_impl(view);
    if (!layout_ptr_)
    {
        return;
    }

    const auto line = layout_ptr_->do_layout(
        view.entry_data, view.entry_size, view.format_string, view.categories, view.category_count
    );

    auto wh = alloc_write_cache(line.size() + 1);
    std::memcpy(wh.data(), line.data(), line.size());
    wh.data()[line.size()] = '\n';
    return_write_cache(wh);
    mark_write_finished();
}

bool appender_file_text::parse_exist_log_file(parse_file_context&)
{
    return true;
}

void appender_file_text::on_file_open(bool is_new_created)
{
    appender_file_base::on_file_open(is_new_created);
}

std::string appender_file_text::get_file_ext_name()
{
    return ".log";
}

bool appender_file_text::on_appender_file_recovery_begin()
{
    if (!appender_file_base::on_appender_file_recovery_begin())
    {
        return false;
    }
    direct_write(k_recover_start, std::strlen(k_recover_start), SEEK_END, 0);
    direct_write("\n", 1, SEEK_CUR, 0);
    return true;
}

void appender_file_text::on_appender_file_recovery_end()
{
    appender_file_base::on_appender_file_recovery_end();
    direct_write(k_recover_end, std::strlen(k_recover_end), SEEK_END, 0);
    direct_write("\n", 1, SEEK_CUR, 0);
}

void appender_file_text::on_log_item_recovery_begin(entry_runtime_view& read_view)
{
    appender_file_base::on_log_item_recovery_begin(read_view);
    auto wh = alloc_write_cache(std::strlen(k_recover_start) + 1);
    std::memcpy(wh.data(), k_recover_start, std::strlen(k_recover_start));
    wh.data()[std::strlen(k_recover_start)] = '\n';
    return_write_cache(wh);
    mark_write_finished();
}

void appender_file_text::on_log_item_recovery_end()
{
    appender_file_base::on_log_item_recovery_end();
    auto wh = alloc_write_cache(std::strlen(k_recover_end) + 1);
    std::memcpy(wh.data(), k_recover_end, std::strlen(k_recover_end));
    wh.data()[std::strlen(k_recover_end)] = '\n';
    return_write_cache(wh);
    mark_write_finished();
}

} // namespace qlog::appender