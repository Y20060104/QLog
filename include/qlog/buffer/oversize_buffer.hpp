#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include "qlog/buffer/log_buffer_defs.hpp"
#include "qlog/buffer/normal_buffer.hpp"
#include "qlog/buffer/siso_ring_buffer.hpp"
#include "qlog/utility/utility_types.hpp"

namespace qlog::buffer {

class oversize_buffer : public normal_buffer {
   private:
    static constexpr std::size_t HEAD_SIZE = qlog::utility::QLOG_CACHE_LINE_SIZE;

    alignas(8) char siso_buffer_obj_[sizeof(siso_ring_buffer)];

   public:
    oversize_buffer(std::uint32_t required_siso_buffer_size, const std::string& mmap_file_abs_path,
                    bool auto_create);

    virtual ~oversize_buffer();

    template <typename T>
    qlog_forceinline T& get_misc_data() {
        static_assert(sizeof(T) <= HEAD_SIZE, "size of T too large");
        return *reinterpret_cast<T*>((void*)data());
    }

    static std::uint32_t calculate_size_of_memory(std::uint32_t required_siso_buffer_size);

    qlog_forceinline log_buffer_write_handle alloc_write_chunk(std::uint32_t size) {
        return get_buffer().alloc_write_chunk(size);
    }

    qlog_forceinline void commit_write_chunk(const log_buffer_write_handle& handle) {
        get_buffer().commit_write_chunk(handle);
    }

    qlog_forceinline log_buffer_read_handle read_chunk() {
        return get_buffer().read_chunk();
    }

    qlog_forceinline log_buffer_read_handle read_an_empty_chunk() {
        return get_buffer().read_an_empty_chunk();
    }

    qlog_forceinline void discard_read_chunk(log_buffer_read_handle& handle) {
        get_buffer().discard_read_chunk(handle);
    }

    qlog_forceinline void return_read_chunk(const log_buffer_read_handle& handle) {
        get_buffer().return_read_chunk(handle);
    }

    qlog_forceinline siso_ring_buffer::siso_buffer_batch_read_handle batch_read() {
        return get_buffer().batch_read();
    }

    qlog_forceinline void return_batch_read_chunks(const siso_ring_buffer::siso_buffer_batch_read_handle& handle) {
        get_buffer().return_batch_read_chunks(handle);
    }

    qlog_forceinline const std::uint8_t* get_buffer_addr() const {
        return get_buffer().get_buffer_addr();
    }

    qlog_forceinline std::uint32_t get_block_size() const {
        return get_buffer().get_block_size();
    }

    qlog_forceinline std::uint32_t get_total_blocks_count() const {
        return get_buffer().get_total_blocks_count();
    }

   private:
    qlog_forceinline siso_ring_buffer& get_buffer() {
        return *(siso_ring_buffer*)siso_buffer_obj_;
    }

    qlog_forceinline const siso_ring_buffer& get_buffer() const {
        return *(const siso_ring_buffer*)siso_buffer_obj_;
    }

    bool try_recover_from_exist_memory_map();

    void init_with_memory_map();

    void init_with_memory();
};

}  // namespace qlog::buffer