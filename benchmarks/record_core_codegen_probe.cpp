#include <qlog/detail/format_hash_reference.hpp>
#include <qlog/detail/record_decoder.hpp>
#include <qlog/detail/record_encoder.hpp>
using namespace qlog::detail;
extern "C" {
__attribute__((noinline)) std::uint32_t qlog_probe_literal(std::byte* out, std::size_t capacity,
                                                           int a, int b,
                                                           const RecordValidationPolicy& policy,
                                                           const FormatHashDispatch& dispatch) {
    static constexpr char fmt[] = "{} {}";
    static constexpr auto stored = hash_literal_stored(fmt);
    const auto m =
        measure_record({reinterpret_cast<const std::byte*>(fmt), 5, stored}, capacity, a, b);
    if (!m.succeeded()) __builtin_trap();
    return encode_v1(out, m.prepared()->payload_size(), *m.prepared(), {}, policy, dispatch)
        .bytes_written();
}
__attribute__((noinline)) std::uint32_t qlog_probe_runtime(std::byte* out, std::size_t capacity,
                                                           const std::byte* fmt, std::size_t n,
                                                           int a, int b,
                                                           const RecordValidationPolicy& policy,
                                                           const FormatHashDispatch& dispatch) {
    const auto m = measure_record({fmt, n, 0}, capacity, a, b);
    if (!m.succeeded()) __builtin_trap();
    return encode_v1(out, m.prepared()->payload_size(), *m.prepared(), {}, policy, dispatch)
        .bytes_written();
}
}
