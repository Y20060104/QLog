#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "qlog/layout/time_zone.hpp"
#include "qlog/record/log_entry_handle.hpp"

namespace qlog::layout {
class Layout {
   private:
    struct FormatInfo {
        bool used = false;
        bool upper = true;

        char fill = ' ';
        char align = '>';
        char sign = '-';
        char prefix = ' ';

        std::uint32_t offset = 0;
        std::uint32_t width = 0;
        std::uint32_t precision = std::numeric_limits<std::uint32_t>::max();

        char type = ' ';

        void reset();
    };

   public:
    enum class enum_layout_result {
        finished,
        to_be_continue,
        parse_error,
    };

    Layout();

    enum_layout_result do_layout(const qlog::record::LogEntryHandle& log_entry,
                                 TimeZone& input_time_zone,
                                 const std::vector<std::string>* categories_name_array_ptr);

    const char* get_formated_str() {
        return format_content_.empty() ? nullptr : format_content_.data();
    }

    std::uint32_t get_formated_str_len() const {
        return format_content_cursor_;
    }

    void tidy_memory();

   private:
    enum_layout_result layout_prefix(const qlog::record::LogEntryHandle& log_entry);

    enum_layout_result insert_time(const qlog::record::LogEntryHandle& log_entry);

    enum_layout_result insert_thread_info(const qlog::record::LogEntryHandle& log_entry);

    void python_style_format_content(const qlog::record::LogEntryHandle& log_entry);

    void python_style_format_content_utf8(const qlog::record::LogEntryHandle& log_entry);

    void python_style_format_content_utf16(const qlog::record::LogEntryHandle& log_entry);

    template <typename Char>
    FormatInfo c20_format(const Char* style, std::int32_t len);

    void fill_and_alignment(std::uint32_t write_begin_pos);

    void fill_e_style(std::uint32_t e_count, std::uint32_t begin_cursor);

    void expand_format_content_buff_size(std::uint32_t new_size);

    std::uint32_t insert_str_utf8(const char* str, const std::uint32_t len);

    std::uint32_t insert_str_utf16(const char* str, const std::uint32_t len);

    void insert_pointer(const void* ptr);

    void insert_bool(bool value);

    void insert_char(char value);

    void insert_char16(char16_t value);

    void insert_char32(char32_t value);

    std::uint32_t insert_integral_unsigned(std::uint64_t value, std::uint32_t base = 10);

    std::uint32_t insert_integral_signed(std::int64_t value, std::uint32_t base = 10);

    void insert_decimal(float value);

    void insert_decimal(double value);

    void reverse(std::uint32_t begin_cursor, std::uint32_t end_cursor);

   private:
    TimeZone* time_zone_ptr_ = nullptr;

    const std::vector<std::string>* categories_name_array_ptr_ = nullptr;

    std::vector<char> format_content_;
    std::uint32_t format_content_cursor_ = 0;

    std::unordered_map<std::uint64_t, std::string> thread_names_cache_;

    FormatInfo format_info_;
};

}  // namespace qlog::layout