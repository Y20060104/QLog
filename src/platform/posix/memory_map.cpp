#include "qlog/platform/posix/memory_map.hpp"

#if !defined(_WIN32)

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cinttypes>
#include <cstring>

#include "qlog/global/common_vars.hpp"
#include "qlog/utility/diagnostics.hpp"

namespace qlog {

static std::size_t get_memory_map_size_unit() {
    return common_global_vars::get().page_size_;
}

bool memory_map::is_platform_support() {
    static_assert(sizeof(std::size_t) <= sizeof(memory_map_handle::platform_data_),
                  "memory_map_handle::platform_data_ size not enough");

#if defined(__ANDROID__) || defined(__APPLE__) || defined(__linux__) || defined(__unix__)
    return true;
#else
    return false;
#endif
}

std::size_t memory_map::get_memory_map_alignedment() {
    static std::size_t __memory_map_size_unit = get_memory_map_size_unit();

    return __memory_map_size_unit;
}

memory_map_handle memory_map::create_memory_map(const qlog::file_handle& map_file,
                                                const std::size_t offset, const std::size_t size) {
    memory_map_handle result;

    if (!map_file.is_valid()) {
        result.error_code_ = EBADF;

        qlog::utility::util::log_device_console_plain_text(
            qlog::LogLevel::error, "create_memory_map with invalid map_file");

        return result;
    }

    auto fd = map_file.platform_handle();

    std::size_t real_mapping_offset = get_real_map_offset(offset);

    std::size_t real_mapping_size = get_real_map_size(offset, size);

    std::size_t alignment_offset = offset - get_real_map_offset(offset);

    std::size_t real_min_file_size = get_min_size_of_memory_map_file(offset, size);

    assert(real_min_file_size == real_mapping_offset + real_mapping_size);

    {
        char chunk[4096];
        std::size_t pos = 0;

        while (pos < real_min_file_size) {
            std::size_t chunk_size = (std::min)(sizeof(chunk), real_min_file_size - pos);
            // 不足以放下chunk？
            ssize_t read_bytes = pread(fd, chunk, chunk_size, (off_t)pos);

            if (read_bytes < 0) {
                if (errno == EINTR) {
                    continue;
                }

                result.error_code_ = errno;

                qlog::utility::util::log_device_console(
                    qlog::LogLevel::warning,
                    "create_memory_map materialize file failed, "
                    "path:%s, error_code:%" PRId32,
                    map_file.abs_file_path().c_str(), result.error_code_);

                return result;
            }

            if ((std::size_t)read_bytes < chunk_size) {  // 这是什么？
                std::memset(chunk + read_bytes, 0, chunk_size - (std::size_t)read_bytes);
            }

            std::size_t written = 0;

            while (written < chunk_size) {
                ssize_t write_bytes =
                    pwrite(fd, chunk + written, chunk_size - written, (off_t)(pos + written));

                if (write_bytes < 0) {
                    if (errno == EINTR) {
                        continue;
                    }

                    result.error_code_ = errno;

                    qlog::utility::util::log_device_console(
                        qlog::LogLevel::warning,
                        "create_memory_map materialize file failed, "
                        "path:%s, error_code:%" PRId32,
                        map_file.abs_file_path().c_str(), result.error_code_);

                    return result;
                }

                written += (std::size_t)write_bytes;
            }

            pos += chunk_size;
        }
    }

    result.real_data_ = mmap(NULL, real_mapping_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
                             static_cast<off_t>(real_mapping_offset));

    if (MAP_FAILED == result.real_data_) {
        result.error_code_ = errno;

        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "create_memory_map file failed, "
                                                "path:%s, error_code:%" PRId32,
                                                map_file.abs_file_path().c_str(),
                                                result.error_code_);

        return result;
    }

    result.mapped_data_ = (void*)((std::uint8_t*)result.real_data_ + alignment_offset);

    result.file_ = map_file;
    result.size_ = size;

    *(std::size_t*)result.platform_data_ = real_mapping_size;

    return result;
}

void memory_map::flush_memory_map(const memory_map_handle& handle) {
#ifndef NDEBUG
    assert(handle.has_been_mapped() &&
           "flush_memory_map can not be called without "
           "create_memory_map and map_to_memory");
#endif

    if (msync(handle.real_data_, *(const std::size_t*)handle.platform_data_, MS_SYNC)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "flush_memory_map file failed, "
                                                "error_code:%" PRId32,
                                                errno);
    }
}

void memory_map::release_memory_map(memory_map_handle& handle) {
    if (!handle.has_been_mapped()) {
        return;
    }

    if (0 != munmap(handle.real_data_, *(const std::size_t*)handle.platform_data_)) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "release_memory_map file failed, "
                                                "error_code:%" PRId32,
                                                errno);
    }

    handle.file_.invalid();

    std::memset(&handle.platform_data_, 0, sizeof(handle.platform_data_));

    handle.mapped_data_ = nullptr;
    handle.real_data_ = nullptr;
    handle.size_ = 0;
    handle.error_code_ = 0;
}

}  // namespace qlog

#endif
