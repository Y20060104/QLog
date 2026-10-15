#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "fixtures/copy_hash_vectors.hpp"
#include "qlog/global/common_vars.hpp"
#include "qlog/log.hpp"
#include "qlog/utility/hash.hpp"
#include "utility/hash_core.hpp"

namespace {
std::size_t checks = 0;
void check(bool value, const char* reason) {
    ++checks;
    if (!value) {
        throw std::runtime_error(reason);
    }
}

template <typename Copy>
void verify_copy(const copy_hash_fixture::hash_vector& test, std::size_t source_offset,
                 std::size_t destination_offset, const std::vector<std::byte>& source,
                 std::uint64_t expected_hash, Copy copy) {
    constexpr std::byte guard{0xA5};
    std::vector<std::byte> destination(destination_offset + test.size + 16U, guard);
    const auto* data = source.empty() ? nullptr : source.data() + source_offset;
    const auto result = copy(data, destination.data() + destination_offset, test.size);
    if (result != expected_hash) {
        std::cerr << "size=" << test.size << " pattern=" << test.pattern
                  << " source_offset=" << source_offset
                  << " destination_offset=" << destination_offset << '\n';
    }
    check(result == expected_hash, "hash differs from original BQLog vector");
    for (std::size_t i = 0; i < destination.size(); ++i) {
        const auto expected = i >= destination_offset && i < destination_offset + test.size
                                  ? source[source_offset + i - destination_offset]
                                  : guard;
        check(destination[i] == expected, "copied bytes or destination guard changed");
    }
}

void utility() {
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
    const bool hardware = qlog::common_global_vars::get().crc32_supported_;
#else
    constexpr bool hardware = false;
#endif
    std::size_t cases = 0;
    for (const auto& test : copy_hash_fixture::vectors) {
        for (std::size_t source_offset = 0; source_offset < 8U; ++source_offset) {
            std::vector<std::byte> source(source_offset + test.size, std::byte{0x3C});
            for (std::size_t i = 0; i < test.size; ++i) {
                source[source_offset + i] = std::byte(test.pattern == 0U ? 0U :
                    static_cast<unsigned char>((i * 131U + 29U + (i >> 3U)) & 255U));
            }
            const auto original_source = source;
            const auto* data = source.empty() ? nullptr : source.data() + source_offset;
            const auto expected_public = hardware ? test.hardware : test.software;
            check(qlog::utility::get_hash_64(data, test.size) == expected_public,
                  "existing hash contract differs from original vector");
            for (std::size_t destination_offset = 0; destination_offset < 8U;
                 ++destination_offset) {
                verify_copy(test, source_offset, destination_offset, source, test.software,
                            qlog::utility::hash_impl::software_copy_raw);
#if defined(__i386__) || defined(__x86_64__) || defined(_M_IX86) || defined(_M_X64)
                if (hardware) {
                    verify_copy(test, source_offset, destination_offset, source, test.hardware,
                                qlog::utility::hash_impl::x86_copy_raw);
                }
#endif
                verify_copy(test, source_offset, destination_offset, source, expected_public,
                            [](const std::byte* src, std::byte* dst, std::size_t len) {
                                return qlog::utility::qlog_memcpy_with_hash(dst, src, len);
                            });
                check(source == original_source, "copy modified its source");
                ++cases;
            }
        }
    }
    check(qlog::utility::qlog_memcpy_with_hash(nullptr, nullptr, 0) == 0,
          "zero-length null buffers must return zero without access");
    std::cout << "vectors=" << copy_hash_fixture::vectors.size() << " cases=" << cases
              << " hardware=" << hardware << '\n';
}

std::mutex capture_mutex;
std::vector<std::string> captured;
void QLOG_STDCALL capture(std::uint64_t, std::int32_t, qlog::LogLevel, const char* text,
                          std::int32_t length) {
    std::lock_guard<std::mutex> lock(capture_mutex);
    captured.emplace_back(text, static_cast<std::size_t>(length));
}

template <typename String>
void write_format(const qlog::log& logger, String& format, qlog::record::ArgumentTag tag) {
    const String original = format;
    const auto size = static_cast<std::uint32_t>(format.size() * sizeof(typename String::value_type));
    const auto expected_hash = qlog::utility::get_hash_64(format.data(), size);
    const auto handle = qlog::runtime::log_write_begin(
        logger.get_id(), static_cast<std::uint8_t>(qlog::LogLevel::info), 0,
        static_cast<std::uint8_t>(tag), size, format.data(), 0);
    check(handle.result == qlog::buffer::enum_buffer_result_code::success, "begin format record");
    const auto* head = reinterpret_cast<const qlog::record::RecordHeader*>(
        handle.format_data_addr - sizeof(qlog::record::RecordHeader));
    check(head->format_hash == expected_hash, "writer stores fused format hash");
    check(head->log_format_data_len == size, "format length remains byte count");
    std::fill(format.begin(), format.end(), static_cast<typename String::value_type>('?'));
    check(std::memcmp(handle.format_data_addr, original.data(), size) == 0,
          "writer owns a copy before source changes");
    qlog::runtime::log_write_finish(logger.get_id(), handle);
}

void writer() {
    qlog::log::register_console_callback(&capture);
    std::size_t mode_index = 0;
    for (const std::string mode : {"sync", "async", "independent"}) {
        const auto logger = qlog::log::create_log(
            "copy_hash_" + mode,
            "log.thread_mode=" + mode +
                "\nappenders_config.console.type=console\nappenders_config.console.levels=[all]\n");
        check(logger.is_valid(), "create writer test logger");
        const std::string expected_u8 = "copy-hash-UTF8-" + std::string(47, 'A');
        const std::string expected_u16 = "copy-hash-UTF16-中文😀" + std::string(47, 'B');
        auto utf8 = expected_u8;
        std::u16string utf16 = u"copy-hash-UTF16-中文😀" + std::u16string(47, u'B');
        write_format(logger, utf8, qlog::record::ArgumentTag::string_utf8_type);
        write_format(logger, utf16, qlog::record::ArgumentTag::string_utf16_type);

        const std::string deferred = "deferred-format";
        const auto size = static_cast<std::uint32_t>(deferred.size());
        const auto handle = qlog::runtime::log_write_begin(
            logger.get_id(), static_cast<std::uint8_t>(qlog::LogLevel::info), 0,
            static_cast<std::uint8_t>(qlog::record::ArgumentTag::string_utf8_type), size, nullptr, 0);
        check(handle.result == qlog::buffer::enum_buffer_result_code::success, "begin deferred record");
        const auto* head = reinterpret_cast<const qlog::record::RecordHeader*>(
            handle.format_data_addr - sizeof(qlog::record::RecordHeader));
        check(head->format_hash == 0, "null format pointer keeps deferred hash rule");
        std::memcpy(handle.format_data_addr, deferred.data(), deferred.size());
        qlog::runtime::log_write_finish(logger.get_id(), handle);
        logger.force_flush();
        std::lock_guard<std::mutex> lock(capture_mutex);
        check(captured.size() == (mode_index + 1U) * 3U, "each mode emits three records");
        check(captured[mode_index * 3U].find(expected_u8) != std::string::npos,
              "UTF-8 output uses copied source");
        check(captured[mode_index * 3U + 1U].find(expected_u16) != std::string::npos,
              "UTF-16 output uses copied source");
        check(captured[mode_index * 3U + 2U].find(deferred) != std::string::npos,
              "deferred output remains available");
        ++mode_index;
    }
    qlog::log::unregister_console_callback(&capture);
}
}  // namespace

int main(int argc, char** argv) {
    try {
        check(argc == 2, "one test group required");
        const std::string group = argv[1];
        if (group == "utility") {
            utility();
        } else if (group == "writer") {
            writer();
        } else {
            throw std::runtime_error("unknown group");
        }
        qlog::log::uninit();
        std::cout << "PASS " << group << " checks=" << checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        qlog::log::uninit();
        return 1;
    }
}
