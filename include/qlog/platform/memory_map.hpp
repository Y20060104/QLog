#pragma once

#include <cstddef>
#include <cstdint>

#include "qlog/io/file_manager.hpp"

namespace qlog {

class memory_map_handle {
    friend class memory_map;

   private:
    std::uint8_t platform_data_[8] = {0, 0, 0, 0, 0, 0, 0, 0};

    void* mapped_data_ = nullptr;
    void* real_data_ = nullptr;

    std::size_t size_ = 0;
    std::int32_t error_code_ = 0;

    file_handle file_;

   public:
    void* get_mapped_data() const {
        return mapped_data_;
    }

    std::size_t get_mapped_size() const {
        return size_;
    }

    std::int32_t get_error_code() const {
        return error_code_;
    }

    bool has_been_mapped() const {
        return static_cast<bool>(mapped_data_);
    }
};

class memory_map {
   public:
    static bool is_platform_support();

    static std::size_t get_memory_map_alignedment();

    static memory_map_handle create_memory_map(const qlog::file_handle& map_file,
                                               const std::size_t offset, const std::size_t size);

    static void flush_memory_map(const memory_map_handle& handle);

    static void release_memory_map(memory_map_handle& handle);

    static std::size_t get_min_size_of_memory_map_file(std::size_t offset, std::size_t size);

   private:
    static std::size_t get_real_map_offset(std::size_t offset);

    static std::size_t get_real_map_size(std::size_t offset, std::size_t size);
};

}  // namespace qlog