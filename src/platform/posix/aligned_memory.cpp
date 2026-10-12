#include "qlog/platform/aligned_memory.hpp"

#include <cstdlib>

namespace qlog::platform {
void* aligned_alloc(std::size_t alignment, std::size_t size) {
    if (alignment < sizeof(void*)) {
        alignment = sizeof(void*);
    }

    void* p = nullptr;
    if (::posix_memalign(&p, alignment, size) != 0) {
        return nullptr;
    }
    return p;
}

void aligned_free(void* ptr) {
    std::free(ptr);
}
}  // namespace qlog::platform