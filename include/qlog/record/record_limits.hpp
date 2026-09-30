#pragma once

#include <cstddef>

#include "qlog/record/record_header.hpp"

namespace qlog::record {
inline constexpr std::size_t kRecordHeaderBytes = sizeof(RecordHeader);
}  // namespace qlog::record
