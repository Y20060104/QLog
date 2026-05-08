#pragma once

#include "qlog/appender/appender_file_binary.h"

namespace qlog::appender
{
class appender_file_raw : public appender_file_binary
{
public:
    static constexpr uint32_t format_version = 6; // 格式化版本号

protected:
    void log_impl(const entry_runtime_view& view) override;
    std::string get_file_ext_name() override;
    uint32_t get_binary_format_version() const override;

    appender_file_binary::appender_format_type get_appender_format() const override
    {
        return appender_file_binary::appender_format_type::raw;
    }
};
} // namespace qlog::appender
