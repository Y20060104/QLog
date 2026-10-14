#include "qlog/buffer/oversize_buffer.hpp"

#include <cstring>

#include "qlog/utility/type_tools.hpp"

namespace qlog::buffer {

oversize_buffer::oversize_buffer(std::uint32_t required_siso_buffer_size,
                                 const std::string& mmap_file_abs_path, bool auto_create)
    : normal_buffer(static_cast<std::size_t>(calculate_size_of_memory(required_siso_buffer_size)),
                    mmap_file_abs_path, auto_create) {
    if (!is_valid()) {
        return;
    }

    auto mmap_result = get_mmap_result();

    switch (mmap_result) {
        case create_memory_map_result::failed:
            qlog::utility::object_constructor<SpscRingBuffer>::construct(
                reinterpret_cast<SpscRingBuffer*>(siso_buffer_obj_),
                static_cast<std::uint8_t*>(data()) + static_cast<std::ptrdiff_t>(HEAD_SIZE),
                calculate_size_of_memory(required_siso_buffer_size) -
                    static_cast<std::uint32_t>(HEAD_SIZE),
                false);

            init_with_memory();
            break;

        case create_memory_map_result::use_existed:
            qlog::utility::object_constructor<SpscRingBuffer>::construct(
                reinterpret_cast<SpscRingBuffer*>(siso_buffer_obj_),
                static_cast<std::uint8_t*>(data()) + static_cast<std::ptrdiff_t>(HEAD_SIZE),
                calculate_size_of_memory(required_siso_buffer_size) -
                    static_cast<std::uint32_t>(HEAD_SIZE),
                true);

            if (!try_recover_from_exist_memory_map()) {
                init_with_memory_map();
            }
            break;

        case create_memory_map_result::new_created:
            qlog::utility::object_constructor<SpscRingBuffer>::construct(
                reinterpret_cast<SpscRingBuffer*>(siso_buffer_obj_),
                static_cast<std::uint8_t*>(data()) + static_cast<std::ptrdiff_t>(HEAD_SIZE),
                calculate_size_of_memory(required_siso_buffer_size) -
                    static_cast<std::uint32_t>(HEAD_SIZE),
                true);

            init_with_memory_map();
            break;

        default:
            break;
    }
}

oversize_buffer::~oversize_buffer() {
    if (!is_valid()) {
        // 构造时跳过了 SISO placement-new。
        // 基类随后释放其拥有的存储。
        return;
    }  // 为什么跳过了sisoplacement new？

    qlog::utility::object_destructor<SpscRingBuffer>::destruct(&get_buffer());
}

std::uint32_t oversize_buffer::calculate_size_of_memory(std::uint32_t required_siso_buffer_size) {
    return SpscRingBuffer::calculate_min_size_of_memory(required_siso_buffer_size) +
           static_cast<std::uint32_t>(HEAD_SIZE);
}

bool oversize_buffer::try_recover_from_exist_memory_map() {
    if (get_buffer().get_memory_map_buffer_state() !=
        MemoryMapBufferState::recover_from_memory_map) {
        return false;
    }

    return true;
}

void oversize_buffer::init_with_memory_map() {
    std::memset(data(), 0, HEAD_SIZE);
}

void oversize_buffer::init_with_memory() {
    std::memset(data(), 0, HEAD_SIZE);
}

}  // namespace qlog::buffer