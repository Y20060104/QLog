#pragma once
#include <cstddef>
#include <cstdint>

#include "qlog/log_level.hpp"
#include "qlog/record/record_header.hpp"

namespace qlog::record {
class LogEntryHandle {
   public:
    LogEntryHandle(const std::uint8_t* data, std::uint32_t size)
        : data_ptr_(data), data_len_(size) {}

    const std::uint8_t* data() const {
        return data_ptr_;
    }
    std::uint32_t data_size() const {
        return data_len_;
    }
    const RecordHeader& get_log_head() const {
        return *reinterpret_cast<const RecordHeader*>(data_ptr_);
    }
    RecordHeader& get_log_head() {  // CR:为什么这里需要const和非const的getloghead？ 为什么会报错？
        return *const_cast<RecordHeader*>(reinterpret_cast<const RecordHeader*>(data_ptr_));
    }

    const char* get_format_string_data() const {
        return reinterpret_cast<const char*>(data_ptr_ + sizeof(RecordHeader));
    }

    std::size_t get_log_args_offset() const {
        const std::size_t format_len = static_cast<std::size_t>(get_log_head().log_format_data_len);
        return sizeof(RecordHeader) + ((format_len + 3U) & ~std::size_t{3});
    }
    const std::uint8_t* get_log_args_data() const {
        return data_ptr_ + get_log_args_offset();
    }
    std::uint32_t get_log_args_data_size() const {
        return static_cast<std::uint32_t>(get_log_head().ext_info_offset - get_log_args_offset());
    }
    const RecordExtHeader& get_ext_head() const {
        return *reinterpret_cast<const RecordExtHeader*>(data_ptr_ +
                                                         get_log_head().ext_info_offset);
    }
    LogLevel get_level() const {
        return static_cast<LogLevel>(get_log_head().level);
    }
    std::uint32_t get_category_idx() const {
        return get_log_head().category_idx;
    }
    bool validate() const;

   private:
    const std::uint8_t* data_ptr_;
    std::uint32_t data_len_;
};
}  // namespace qlog::record