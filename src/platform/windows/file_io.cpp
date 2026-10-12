#include "qlog/platform/file_io.hpp"

#include <direct.h>
#include <windows.h>

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cwchar>

#include "qlog/global/common_vars.hpp"
#include "qlog/platform/file_path.hpp"
#include "qlog/platform/mutex.hpp"
#include "qlog/platform/windows/file_path_internal.hpp"
#include "qlog/utility/diagnostics.hpp"
#include "qlog/utility/hash.hpp"
#include "qlog/utility/utf_conversion.hpp"

namespace qlog::platform {
std::int32_t get_file_size(const platform_file_handle& file_handle, std::size_t& size_ref) {
    size_ref = 0;

    LARGE_INTEGER file_size;
    if (!::GetFileSizeEx(file_handle, &file_size)) {
        return static_cast<std::int32_t>(::GetLastError());
    }

    size_ref = static_cast<std::size_t>(file_size.QuadPart);
    return 0;
}

std::int32_t read_file(const platform_file_handle& file_handle, void* target_addr,
                       std::size_t read_size, std::size_t& out_real_read_size) {
    out_real_read_size = 0;

    const std::size_t max_size_pertime = static_cast<std::size_t>(UINT32_MAX);
    char* current_target = static_cast<char*>(target_addr);

    while (out_real_read_size < read_size) {
        const std::size_t need_read_this_time =
            (std::min)(max_size_pertime, read_size - out_real_read_size);

        DWORD out_size = 0;
        if (!::ReadFile(file_handle, current_target, static_cast<DWORD>(need_read_this_time),
                        &out_size, nullptr)) {
            // CR:dword是什么？
            return static_cast<std::int32_t>(::GetLastError());
        }

        out_real_read_size += static_cast<std::size_t>(out_size);
        current_target += out_size;

        if (static_cast<std::size_t>(out_size) != need_read_this_time) {
            return 0;
        }
    }

    return 0;
}

std::int32_t write_file(const platform_file_handle& file_handle, const void* src_addr,
                        std::size_t write_size, std::size_t& out_real_write_size) {
    out_real_write_size = 0;

    const char* current_src = static_cast<const char*>(src_addr);
    std::size_t remaining = write_size;

    while (remaining > 0) {
        const DWORD chunk_size =
            static_cast<DWORD>((std::min)(remaining, static_cast<std::size_t>(UINT32_MAX)));

        DWORD written = 0;
        if (!::WriteFile(file_handle, current_src, chunk_size, &written, nullptr)) {
            return static_cast<std::int32_t>(::GetLastError());
        }

        out_real_write_size += static_cast<std::size_t>(written);
        current_src += static_cast<std::size_t>(written);
        remaining -= static_cast<std::size_t>(written);

        if (written == 0) {
            break;
        }
    }

    return 0;
}

std::int32_t seek_file(const platform_file_handle& file_handle, file_seek_option opt,
                       std::int64_t offset) {
    LONG offset_low = static_cast<LONG>(offset & 0xFFFFFFFFLL);
    LONG offset_high = static_cast<LONG>(offset >> 32);

    DWORD opt_platform = FILE_CURRENT;

    switch (opt) {
        case file_seek_option::current:
            opt_platform = FILE_CURRENT;
            break;

        case file_seek_option::begin:
            opt_platform = FILE_BEGIN;
            break;

        case file_seek_option::end:
            opt_platform = FILE_END;
            break;
        default:
            break;
    }

    ::SetLastError(NO_ERROR);

    if (::SetFilePointer(file_handle, offset_low, &offset_high, opt_platform) ==
            INVALID_SET_FILE_POINTER &&
        ::GetLastError() != NO_ERROR) {
        return static_cast<std::int32_t>(::GetLastError());
    }

    return 0;
}

std::int32_t truncate_file(const platform_file_handle& file_handle, std::size_t offset) {
    FILE_END_OF_FILE_INFO eof_info;
    eof_info.EndOfFile.QuadPart = static_cast<LONGLONG>(offset);

    if (!::SetFileInformationByHandle(file_handle, FileEndOfFileInfo, &eof_info,
                                      static_cast<DWORD>(sizeof(eof_info)))) {
        return static_cast<std::int32_t>(::GetLastError());
    }

    return 0;
}

std::int32_t flush_file(const platform_file_handle& file_handle) {
    if (!::FlushFileBuffers(file_handle)) {
        return static_cast<std::int32_t>(::GetLastError());
    }

    return 0;
}

bool lock_file(const platform_file_handle& file_handle) {
    if (!is_platform_handle_valid(file_handle)) {
        return false;
    }

    return ::LockFile(file_handle, 0, 0, 0xFFFFFFFFU, 0xFFFFFFFFU) != 0;
}

bool unlock_file(const platform_file_handle& file_handle) {
    if (!is_platform_handle_valid(file_handle)) {
        return false;
    }

    return UnlockFile(file_handle, 0, 0, 0xFFFFFFFFU, 0xFFFFFFFFU) != 0;
}

std::uint64_t file_node_info::hash_code() const {
    std::uint8_t buf[sizeof(volumn) + sizeof(idx_high) + sizeof(idx_low)];
    std::memcpy(buf, &volumn, sizeof(volumn));
    std::memcpy(buf + sizeof(volumn), &idx_high, sizeof(idx_high));
    std::memcpy(buf + sizeof(volumn) + sizeof(idx_high), &idx_low, sizeof(idx_low));
    return qlog::utility::get_hash_64(buf, sizeof(buf));
}

using windows::trans_to_windows_wide_string;

static std::string force_to_abs_path(const std::string path) {
    if (is_absolute(path)) {
        return path;
    }

    const std::string& base_dir = get_base_dir(0);

    if (!path.empty() && (path[0] == '\\' || path[0] == '/')) {
        return base_dir + path.substr(1, path.size() - 1);
    } else {
        return base_dir + path;
    }
}

static bool get_stat_by_path(const std::string& path, WIN32_FILE_ATTRIBUTE_DATA& buf) {
    std::u16string file_path_w =
        u"\\\\?\\" + trans_to_windows_wide_string(force_to_abs_path(get_lexically_path(path)));

    if (!GetFileAttributesExW(reinterpret_cast<LPCWSTR>(file_path_w.c_str()), GetFileExInfoStandard,
                              &buf)) {
        return false;
    }

    return true;
}

std::int32_t get_file_size(const char* file_path, std::size_t& size_ref) {
    std::u16string file_path_w =
        u"\\\\?\\" + trans_to_windows_wide_string(force_to_abs_path(get_lexically_path(file_path)));

    HANDLE file =
        CreateFileW(reinterpret_cast<LPCWSTR>(file_path_w.c_str()), GENERIC_READ, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (file == INVALID_HANDLE_VALUE) {
        return ERROR_FILE_NOT_FOUND;
    }

    std::int32_t result = get_file_size(file, size_ref);
    CloseHandle(file);

    return result;
}

bool is_dir(const char* path) {
    WIN32_FILE_ATTRIBUTE_DATA buf;

    if (get_stat_by_path(path, buf)) {
        if (buf.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            return true;
        }
    }

    return false;
}

bool is_regular_file(const char* path) {
    WIN32_FILE_ATTRIBUTE_DATA buf;

    if (get_stat_by_path(path, buf)) {
        if (!(buf.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            !(buf.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            return true;
        }
    }

    return false;
}

static std::int32_t make_dir_recursive(char16_t* path) {
    constexpr std::size_t prefix_size = 2;

    if ((std::wcslen(reinterpret_cast<LPCWSTR>(path)) == prefix_size + 2) &&
        path[prefix_size + 1] == ':') {
        return 0;
    }

    WIN32_FILE_ATTRIBUTE_DATA buf;

    if (GetFileAttributesExW(reinterpret_cast<LPCWSTR>(path), GetFileExInfoStandard, &buf)) {
        if (buf.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            return 0;
        }
    }

    LPWSTR ptr = std::wcsrchr(reinterpret_cast<LPWSTR>(path), '\\');

    if (ptr != nullptr) {
        *ptr = u'\0';
        std::int32_t make_parent_result = make_dir_recursive(path);
        *ptr = u'\\';

        if (make_parent_result != 0) {
            return make_parent_result;
        }
    }

    if (_wmkdir(reinterpret_cast<LPCWSTR>(path)) == 0) {
        return 0;
    }

    if (errno == EEXIST) {
        return 0;
    }

    return errno;
}

std::int32_t make_dir(const char* path) {
    if (is_dir(path)) {
        return 0;
    }

    std::u16string path_w =
        u"\\\\?\\" + trans_to_windows_wide_string(force_to_abs_path(get_lexically_path(path)));

    std::int32_t result = make_dir_recursive(path_w.data());
    return result;
}

std::int32_t remove_dir_or_file_inner(std::u16string& path) {
    WIN32_FILE_ATTRIBUTE_DATA buf;

    if (!GetFileAttributesExW(reinterpret_cast<LPCWSTR>(path.c_str()), GetFileExInfoStandard,
                              &buf)) {
        DWORD err = GetLastError();

        return (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) ? ENOENT : EACCES;
    }

    if (!(buf.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
        DWORD attr = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.c_str()));

        attr &= static_cast<DWORD>(~FILE_ATTRIBUTE_READONLY);

        SetFileAttributesW(reinterpret_cast<LPCWSTR>(path.c_str()), attr);

        std::int32_t result = _wremove(reinterpret_cast<LPCWSTR>(path.c_str()));

        if (result != 0) {
            return errno;
        }

        return 0;
    } else {
        std::size_t path_init_size = path.size();

        path.push_back(u'\\');
        path.push_back(u'*');

        WIN32_FIND_DATAW find_data;
        HANDLE h_file = FindFirstFileW(reinterpret_cast<LPCWSTR>(path.c_str()), &find_data);

        path.resize(path.size() - 2);

        if (h_file == INVALID_HANDLE_VALUE) {
            // Reference treats this as empty.
        } else {
            do {
                if (std::wcscmp(find_data.cFileName, reinterpret_cast<LPCWSTR>(u".")) == 0 ||
                    std::wcscmp(find_data.cFileName, reinterpret_cast<LPCWSTR>(u"..")) == 0) {
                    continue;
                }

                path.push_back(u'\\');
                path += reinterpret_cast<const char16_t*>(find_data.cFileName);

                std::int32_t result = remove_dir_or_file_inner(path);
                path.resize(path_init_size);

                if (result != 0) {
                    FindClose(h_file);
                    return result;
                }
            } while (FindNextFileW(h_file, &find_data));

            FindClose(h_file);
        }

        DWORD attr = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.c_str()));

        attr &= static_cast<DWORD>(~FILE_ATTRIBUTE_READONLY);

        SetFileAttributesW(reinterpret_cast<LPCWSTR>(path.c_str()), attr);

        if (!RemoveDirectoryW(reinterpret_cast<LPCWSTR>(path.c_str()))) {
            return static_cast<std::int32_t>(GetLastError());
        }

        return 0;
    }
}

std::int32_t remove_dir_or_file(const char* path) {
    std::u16string path_w =
        trans_to_windows_wide_string(force_to_abs_path(get_lexically_path(path)));

    if (path_w.empty()) {
        return 0;
    }

    path_w = u"\\\\?\\" + path_w;
    return remove_dir_or_file_inner(path_w);
}

static bool add_file_execlusive_check(const platform_file_handle& file_handle,
                                      file_open_mode_enum mode) {
    if (!(std::int32_t)(mode & (file_open_mode_enum::write | file_open_mode_enum::exclusive))) {
        return true;
    }

    BY_HANDLE_FILE_INFORMATION file_info;

    if (!GetFileInformationByHandle(file_handle, &file_info)) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::error, "add_file_execlusive_check GetFileInformationByHandle failed");

        return false;
    }

    auto& file_exclusive_cache = common_global_vars::get().file_exclusive_cache_;

    qlog::platform::scoped_mutex lock(common_global_vars::get().file_exclusive_mutex_);

    file_node_info node_info;
    node_info.volumn = static_cast<std::uint32_t>(file_info.dwVolumeSerialNumber);
    node_info.idx_high = static_cast<std::uint32_t>(file_info.nFileIndexHigh);
    node_info.idx_low = static_cast<std::uint32_t>(file_info.nFileIndexLow);

    auto iter = file_exclusive_cache.find(node_info);

    if (iter == file_exclusive_cache.end()) {
        file_exclusive_cache.emplace(node_info, mode);
        return true;
    } else {
        if ((std::int32_t)((iter->second | mode) & file_open_mode_enum::exclusive)) {
            return false;
        }
    }

    return true;
}

static void remove_file_execlusive_check(const platform_file_handle& file_handle) {
    BY_HANDLE_FILE_INFORMATION file_info;

    if (!GetFileInformationByHandle(file_handle, &file_info)) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::error,
            "remove_file_execlusive_check GetFileInformationByHandle failed");

        return;
    }

    auto& file_exclusive_cache = common_global_vars::get().file_exclusive_cache_;

    qlog::platform::scoped_mutex lock(common_global_vars::get().file_exclusive_mutex_);

    file_node_info node_info;
    node_info.volumn = static_cast<std::uint32_t>(file_info.dwVolumeSerialNumber);
    node_info.idx_high = static_cast<std::uint32_t>(file_info.nFileIndexHigh);
    node_info.idx_low = static_cast<std::uint32_t>(file_info.nFileIndexLow);

    file_exclusive_cache.erase(node_info);
}

std::int32_t open_file(const char* path, file_open_mode_enum mode,
                       platform_file_handle& out_file_handle) {
    std::u16string file_path_w =
        u"\\\\?\\" + trans_to_windows_wide_string(force_to_abs_path(get_lexically_path(path)));

    out_file_handle = CreateFileW(
        reinterpret_cast<LPCWSTR>(file_path_w.c_str()),
        ((std::int32_t)(mode & file_open_mode_enum::read) ? GENERIC_READ : 0) |
            ((std::int32_t)(mode & file_open_mode_enum::write) ? GENERIC_WRITE : 0),
        ((std::int32_t)(mode & file_open_mode_enum::exclusive)
             ? FILE_SHARE_READ
             : (FILE_SHARE_READ | FILE_SHARE_WRITE)),
        nullptr,
        ((std::int32_t)(mode & file_open_mode_enum::auto_create) ? OPEN_ALWAYS : OPEN_EXISTING),
        FILE_ATTRIBUTE_NORMAL, nullptr);

    if (!is_platform_handle_valid(out_file_handle)) {
        return static_cast<std::int32_t>(GetLastError());
    }

    if (!add_file_execlusive_check(out_file_handle, mode)) {
        CloseHandle(out_file_handle);
        out_file_handle = invalid_platform_file_handle;
        return ERROR_SHARING_VIOLATION;
    }

    return 0;
}

std::int32_t close_file(platform_file_handle& in_out_file_handle) {
    remove_file_execlusive_check(in_out_file_handle);

    bool result = CloseHandle(in_out_file_handle) != 0;
    in_out_file_handle = invalid_platform_file_handle;

    if (result) {
        return 0;
    }

    return static_cast<std::int32_t>(GetLastError());
}

std::uint64_t get_file_last_modified_epoch_ms(const char* path) {
    std::u16string file_path_w =
        u"\\\\?\\" + trans_to_windows_wide_string(force_to_abs_path(get_lexically_path(path)));

    WIN32_FILE_ATTRIBUTE_DATA attr_data;

    if (!GetFileAttributesExW(reinterpret_cast<LPCWSTR>(file_path_w.c_str()), GetFileExInfoStandard,
                              &attr_data)) {
        return 0;
    }

    if (attr_data.dwFileAttributes == INVALID_FILE_ATTRIBUTES) {
        return 0;
    }

    ULARGE_INTEGER uli;
    uli.LowPart = attr_data.ftLastWriteTime.dwLowDateTime;
    uli.HighPart = attr_data.ftLastWriteTime.dwHighDateTime;

    std::uint64_t epoch_ms = uli.QuadPart / 10000;
    epoch_ms -= 11644473600000ULL;

    return epoch_ms;
}

std::vector<std::string> get_all_sub_names(const char* path) {
    std::u16string file_path_w =
        u"\\\\?\\" + trans_to_windows_wide_string(force_to_abs_path(get_lexically_path(path)));

    std::vector<std::string> list;
    WIN32_FIND_DATAW find_data;

    HANDLE h_file =
        FindFirstFileW(reinterpret_cast<LPCWSTR>((file_path_w + u"\\*").c_str()), &find_data);

    if (h_file == INVALID_HANDLE_VALUE) {
        // Reference treats this as empty.
    } else {
        do {
            if (std::wcscmp(find_data.cFileName, reinterpret_cast<LPCWSTR>(u".")) == 0 ||
                std::wcscmp(find_data.cFileName, reinterpret_cast<LPCWSTR>(u"..")) == 0) {
                continue;
            }

            char name_utf8_tmp[MAX_PATH * 3 + 2];

            std::uint32_t trans_len = qlog::utility::utf16_to_utf8_sw(
                reinterpret_cast<const char16_t*>(find_data.cFileName),
                (std::uint32_t)std::wcslen(find_data.cFileName), name_utf8_tmp,
                sizeof(name_utf8_tmp));

            assert(trans_len < (std::uint32_t)sizeof(name_utf8_tmp) &&
                   "get_all_sub_names qlog::utility::utf16_to_utf8_sw size error");

            name_utf8_tmp[static_cast<std::size_t>(trans_len)] = u'\0';
            list.push_back(name_utf8_tmp);
        } while (FindNextFileW(h_file, &find_data));

        FindClose(h_file);
    }

    return list;
}
}  // namespace qlog::platform
