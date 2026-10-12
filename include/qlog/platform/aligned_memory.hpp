#pragma once

#include <cstddef>

namespace qlog::platform {
void* aligned_alloc(std::size_t alignment, std::size_t size);
void aligned_free(void* ptr);
}  // namespace qlog::platform