#pragma once
#include <cstdint>

namespace qlog::detail {
enum class CallGate : std::uint8_t {
    proceed,
    filtered,
    invalid_level,
    invalid_category,
};
}