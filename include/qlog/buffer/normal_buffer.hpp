#pragma once

#include <cstddef>
#include <string>

#include "qlog/buffer/log_buffer_defs.hpp"
#include "qlog/io/file_manager.hpp"
#include "qlog/platform/memory_map.hpp"

namespace qlog::buffer {

class normal_buffer {
   private:
    qlog::file_handle memory_map_file_;
    qlog::memory_map_handle memory_map_handle_;
    void* buffer_data_;
    std::size_t buffer_size_;
    create_memory_map_result mmap_result_;
    bool delete_mmap_when_destruct_;

   public:
    normal_buffer(std::size_t size, const std::string& mmap_file_abs_path = "",
                  bool auto_create = true);

    virtual ~normal_buffer();

    std::string get_mmap_file_path() const {
        return memory_map_file_.abs_file_path();
    }

    qlog_forceinline void* data() {
        return buffer_data_;
    }

    qlog_forceinline const void* data() const {
        return buffer_data_;
    }

    qlog_forceinline std::size_t size() const {
        return buffer_size_;
    }

    qlog_forceinline create_memory_map_result get_mmap_result() const {
        return mmap_result_;
    }

    qlog_forceinline void set_delete_mmap_when_destruct(bool delete_mmap) {
        delete_mmap_when_destruct_ = delete_mmap;
    }

    qlog_forceinline bool is_valid() const {
        return buffer_data_ != nullptr;
    }

    void resize(std::size_t new_size);

    bool is_memory_mapped() const {
        return memory_map_handle_.has_been_mapped();
    }

   private:
    create_memory_map_result create_memory_map(std::size_t size,
                                               const std::string& mmap_file_abs_path,
                                               bool auto_create);
};  // 为什么get_mmap_result() == failed 不代表整个 NormalBuffer 无效，仍可能已经成功回退到堆内存
    // 可以回退？
}  // namespace qlog::buffer