#include "qlog/platform/windows/memory_map.hpp"

#if defined(_WIN32)

#include <windows.h>

#include <cassert>
#include <cinttypes>
#include <cstring>

#include "qlog/global/common_vars.hpp"
#include "qlog/utility/diagnostics.hpp"

namespace qlog {

static std::size_t get_memory_map_size_unit() {
    return common_global_vars::get().page_size_;
}

bool memory_map::is_platform_support() {
    static_assert(sizeof(HANDLE) <= sizeof(memory_map_handle::platform_data_),
                  "memory_map_handle::platform_data_ size not enough");

    return true;
}

std::size_t memory_map::get_memory_map_alignedment() {
    static std::size_t __memory_map_size_unit = get_memory_map_size_unit();

    return __memory_map_size_unit;
}

memory_map_handle memory_map::create_memory_map(const qlog::file_handle& map_file,
                                                const std::size_t offset, const std::size_t size) {
    memory_map_handle result;

    if (!map_file.is_valid()) {
        result.error_code_ = ERROR_INVALID_HANDLE;

        qlog::utility::util::log_device_console_plain_text(
            qlog::LogLevel::error, "create_memory_map with invalid map_file");

        return result;
    }

    HANDLE file_handle = map_file.platform_handle();

    HANDLE& memory_map_handle = *(HANDLE*)(result.platform_data_);

    std::size_t real_mapping_offset = get_real_map_offset(offset);

    std::size_t real_mapping_size = get_real_map_size(offset, size);

    std::size_t alignment_offset = offset - get_real_map_offset(offset);

    std::size_t real_min_file_size = get_min_size_of_memory_map_file(offset, size);

    LARGE_INTEGER file_size;

    if (!GetFileSizeEx(file_handle, &file_size)) {
        result.error_code_ = static_cast<std::int32_t>(GetLastError());

        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "create_memory_map GetFileSize failed, "
                                                "path:%s, error_code:%" PRId32,
                                                map_file.abs_file_path().c_str(),
                                                result.error_code_);

        return result;
    }

    std::size_t current_file_size = static_cast<std::size_t>(file_size.QuadPart);

    if (real_min_file_size > 0 && current_file_size <= real_min_file_size) {
        FILE_ALLOCATION_INFO allocation_info;

        allocation_info.AllocationSize.QuadPart = static_cast<LONGLONG>(real_min_file_size);

        if (!SetFileInformationByHandle(file_handle, FileAllocationInfo, &allocation_info,
                                        sizeof(allocation_info))) {
            result.error_code_ = static_cast<std::int32_t>(GetLastError());

            qlog::utility::util::log_device_console(qlog::LogLevel::warning,
                                                    "create_memory_map preallocate file failed, "
                                                    "path:%s, error_code:%" PRId32,
                                                    map_file.abs_file_path().c_str(),
                                                    result.error_code_);

            return result;
        }
    }

    if (current_file_size < real_min_file_size) {
        FILE_END_OF_FILE_INFO eof_info;

        eof_info.EndOfFile.QuadPart = static_cast<LONGLONG>(real_min_file_size);

        if (!SetFileInformationByHandle(file_handle, FileEndOfFileInfo, &eof_info,
                                        sizeof(eof_info))) {
            result.error_code_ = static_cast<std::int32_t>(GetLastError());

            qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                    "create_memory_map set file size failed, "
                                                    "path:%s, error_code:%" PRId32,
                                                    map_file.abs_file_path().c_str(),
                                                    result.error_code_);

            return result;
        }
    }

    DWORD new_size_high = (DWORD)(real_min_file_size >> 32);

    DWORD new_size_low = (DWORD)(real_min_file_size & 0xFFFFFFFF);

    memory_map_handle =
        CreateFileMapping(file_handle, NULL, PAGE_READWRITE, new_size_high, new_size_low, NULL);

    if (!memory_map_handle) {
        result.error_code_ = static_cast<std::int32_t>(GetLastError());

        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "create_memory_map file failed, "
                                                "path:%s, error_code:%" PRId32,
                                                map_file.abs_file_path().c_str(),
                                                result.error_code_);

        return result;
    }

    result.real_data_ = MapViewOfFile(
        memory_map_handle, FILE_MAP_READ | FILE_MAP_WRITE, (DWORD)(real_mapping_offset >> 32),
        (DWORD)(real_mapping_offset & 0x00000000FFFFFFFF), real_mapping_size);

    if (!result.real_data_) {
        CloseHandle(memory_map_handle);

        memory_map_handle = 0;

        result.error_code_ = static_cast<std::int32_t>(GetLastError());

        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "map_to_memory file failed, "
                                                "path:%s, error_code:%" PRId32,
                                                map_file.abs_file_path().c_str(),
                                                result.error_code_);

        return result;
    }

    result.mapped_data_ = (void*)((std::uint8_t*)result.real_data_ + alignment_offset);

    result.file_ = map_file;
    result.size_ = size;

    return result;
}

void memory_map::flush_memory_map(const memory_map_handle& handle) {
#ifndef NDEBUG
    assert(handle.has_been_mapped() &&
           "flush_memory_map can not be called without "
           "create_memory_map and map_to_memory");
#endif

    FlushViewOfFile(handle.real_data_,
                    handle.size_ + static_cast<std::size_t>((std::uint8_t*)handle.mapped_data_ -
                                                            (std::uint8_t*)handle.real_data_));
}

void memory_map::release_memory_map(memory_map_handle& handle) {
    if (!handle.has_been_mapped()) {
        return;
    }

    UnmapViewOfFile(handle.real_data_);

    HANDLE& memory_map_handle = *(HANDLE*)(handle.platform_data_);

    CloseHandle(memory_map_handle);

    handle.file_.invalid();

    std::memset(&handle.platform_data_, 0, sizeof(handle.platform_data_));

    handle.mapped_data_ = nullptr;
    handle.real_data_ = nullptr;
    handle.size_ = 0;
    handle.error_code_ = 0;
}

}  // namespace qlog

#endif