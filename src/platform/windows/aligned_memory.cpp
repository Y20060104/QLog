#include "qlog/platform/aligned_memory.hpp"

#include <malloc.h>

#include <cstdlib>

namespace qlog::platform {
void* aligned_alloc(std::size_t alignment, std::size_t size) {
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }

    return ::_aligned_malloc(size, alignment);
}

void aligned_free(void* ptr) {
    ::_aligned_free(ptr);
}
}  // namespace qlog::platform