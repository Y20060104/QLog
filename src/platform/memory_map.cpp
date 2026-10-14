#include "qlog/platform/memory_map.hpp"

namespace qlog {

std::size_t memory_map::get_real_map_offset(std::size_t offset) {
    std::size_t alignment_offset = offset % get_memory_map_alignedment();

    std::size_t real_mapping_offset = offset - alignment_offset;

    return real_mapping_offset;
}

std::size_t memory_map::get_real_map_size(std::size_t offset, std::size_t size) {
    std::size_t alignment_offset = offset % get_memory_map_alignedment();

    std::size_t real_mapping_size = size + alignment_offset;

    real_mapping_size += (get_memory_map_alignedment() - 1);

    real_mapping_size -= (real_mapping_size % get_memory_map_alignedment());

    return real_mapping_size;
}

std::size_t memory_map::get_min_size_of_memory_map_file(std::size_t offset, std::size_t size) {
    return get_real_map_offset(offset) + get_real_map_size(offset, size);
}

}  // namespace qlog