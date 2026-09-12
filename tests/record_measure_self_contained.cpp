#include <qlog/detail/record_measure.hpp>

static_assert(qlog::detail::kRecordHeaderBytes == 32U);

namespace {

[[maybe_unused]] void instantiate_empty_measure() {
    const auto result = qlog::detail::measure_record(qlog::detail::FormatInput{}, 32U);
    (void)result;
}

}  // namespace
