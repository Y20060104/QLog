#include <qlog/buffer/spsc_ring_buffer.hpp>
#include <qlog/record/log_entry_handle.hpp>
#include <qlog/record/argument_serialization.hpp>
#ifdef EXPECT_DEBUG
#ifndef QLOG_DEBUG
#error QLOG_DEBUG did not propagate to the consumer
#endif
#else
#ifdef QLOG_DEBUG
#error QLOG_DEBUG must be undefined outside Debug
#endif
#endif
static_assert(qlog::record::detail::get_log_param_type_enum<std::u32string>() ==
              qlog::record::ArgumentTag::string_utf16_type);
int main() {
    qlog::record::LogEntryHandle handle(nullptr, 0);
    // Calling a non-inline function forces the Record object out of libqlog.a.
    return handle.validate() ? 1 : 0;
}
