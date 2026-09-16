#pragma once
#include <cassert>
#include<exception>
#include "qlog/log_result.hpp"
#include "qlog/detail/context_result.hpp"
#include "qlog/detail/record_measure.hpp"
#include "qlog/detail/record_types.hpp"
#include "qlog/detail/spsc_ring_buffer.hpp"

namespace qlog::detail{
    [[nodiscard]] inline LogResult map_context_error(ContextError error) noexcept;
[[nodiscard]] inline LogResult map_measure_failure(const MeasureFailure& failure) noexcept;
[[nodiscard]] inline LogResult map_reserve_error(ReserveStatus status,
                                                std::size_t requested) noexcept;
[[nodiscard]] inline LogResult map_encode_error(EncodeError error,
                                               std::size_t target_size) noexcept;

}