#pragma once

#include "qlog/appender/appender_file_base.h"

namespace qlog::appender
{

class appender_file_text : public appender_file_base
{
protected:
    void log_impl(const entry_runtime_view& view) override;
    bool parse_exist_log_file(parse_file_context& context) override;
    void on_file_open(bool is_new_created) override;
    std::string get_file_ext_name() override;

    bool on_appender_file_recovery_begin() override;
    void on_appender_file_recovery_end() override;
    void on_log_item_recovery_begin(entry_runtime_view& read_view) override;
    void on_log_item_recovery_end() override;
};

} // namespace qlog::appender