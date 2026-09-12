#include "support/i1d_properties.hpp"
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    if (size > 65536U) return 0;
    i1d::cstr(data, size);
    return 0;
}
