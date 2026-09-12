#pragma once
#include <cstddef>

#include "qlog/detail/record_types.hpp"

namespace qlog::detail {
[[nodiscard]] DecodeResult decode_v1(const std::byte* payload_data, std::size_t payload_size,
                                     DecodedArg* workspace, std::size_t workspace_count,
                                     const RecordValidationPolicy& policy) noexcept;
}