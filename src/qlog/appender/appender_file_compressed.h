#pragma once

#include "qlog/appender/appender_file_binary.h"

#include <cstdint>
#include <tuple>
#include <unordered_map>
#include <vector>

namespace qlog::appender
{

class appender_file_compressed : public appender_file_binary
{
    enum item_type : uint8_t
    {
        log_template = 0,
        log_entry = 128
    };

    enum template_sub_type : uint8_t
    {
        format_template_utf8 = 0,
        thread_info_template = 1,
        format_template_utf16 = 2
    };

public:
    static constexpr uint32_t format_version = 9;

protected:
    bool init_impl(const appender_config& config) override;
    void log_impl(const entry_runtime_view& view) override;
    bool parse_exist_log_file(parse_file_context& context) override;
    void on_file_open(bool is_new_created) override;

    appender_file_binary::appender_format_type get_appender_format() const override
    {
        return appender_file_binary::appender_format_type::compressed;
    }

    std::string get_file_ext_name() override;
    uint32_t get_binary_format_version() const override;

private:
    using read_item_result =
        std::tuple<bool, item_type, appender_file_base::read_with_cache_handle>;

    read_item_result read_item_data(parse_file_context& context);
    bool parse_log_entry(
        parse_file_context& context, const appender_file_base::read_with_cache_handle& data_handle
    );
    bool parse_format_template(
        parse_file_context& context,
        const appender_file_base::read_with_cache_handle& data_handle,
        template_sub_type sub_type
    );
    bool parse_thread_info_template(
        parse_file_context& context, const appender_file_base::read_with_cache_handle& data_handle
    );
    void reset();
    void write_item(item_type type, const std::vector<uint8_t>& body);

private:
    std::unordered_map<uint64_t, uint32_t> format_templates_hash_cache_{};
    uint32_t current_format_template_max_index_ = 0;
    std::unordered_map<uint64_t, uint32_t> thread_info_hash_cache_{};
    uint32_t current_thread_info_max_index_ = 0;
    uint64_t last_log_entry_epoch_ = 0;
};

} // namespace qlog::appender