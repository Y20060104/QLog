#include "qlog/io/file_manager.hpp"

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>

#include "qlog/global/common_vars.hpp"
#include "qlog/platform/file_io.hpp"
#include "qlog/platform/file_path.hpp"
#include "qlog/utility/diagnostics.hpp"
#include "qlog/utility/string_utils.hpp"

namespace qlog {
static thread_local std::int32_t file_manager_file_error_no_ = 0;

std::string file_manager::get_directory_from_path(const std::string& path) {
    std::string::size_type last_pos1 = path.find_last_of('/');
    std::string::size_type last_pos2 = path.find_last_of('\\');

    if (last_pos1 == 0) {
        last_pos1 = std::string::npos;
    }
    if (last_pos2 == 0) {
        last_pos2 = std::string::npos;
    }

    auto last_split_pos = std::min(last_pos1, last_pos2);

    if (last_split_pos == std::string::npos) {
        return "./";
    }

    std::string result = path.substr(0, last_split_pos);
    std::replace(result.begin(), result.end(), '\\',
                 '/');  // CR:这是在干什么？寻找 / 和 \\ 然后替换为 /？
    return result;
}

std::string file_manager::get_file_name_from_path(const std::string& path) {
    std::string::size_type last_pos1 = path.find_last_of('/');
    std::string::size_type last_pos2 = path.find_last_of('\\');
    if (last_pos1 == 0) {
        last_pos1 = std::string::npos;
    }
    if (last_pos2 == 0) {
        last_pos2 = std::string::npos;
    }

    const auto last_split_pos = std::min(last_pos1, last_pos2);

    if (last_split_pos == std::string::npos) {
        return path;
    }

    if (last_split_pos == path.size() - 1) {
        return "";
    }

    return path.substr(last_split_pos + 1);
}

std::string file_manager::combine_path(const std::string& path1, const std::string& path2) {
    std::string result;
    result.reserve(path1.size() + path2.size() + 1);

    std::string normalized_path1 = path1;
    std::string normalized_path2 = path2;

    std::replace(normalized_path1.begin(), normalized_path1.end(), '\\', '/');
    std::replace(normalized_path2.begin(), normalized_path2.end(), '\\', '/');

    auto split1 = qlog::utility::split_nonempty(normalized_path1, '/');
    auto split2 = qlog::utility::split_nonempty(normalized_path2, '/');

    auto total_parts = split1.size() + split2.size();
    decltype(total_parts) current_parts = 0;

    for (auto part : split1) {
        if (current_parts != 0) {
            result += "/";
        }
        result += part;
        ++current_parts;
    }

    for (auto part : split2) {
        if (current_parts != 0) {
            result += "/";
        }
        result += part;
        ++current_parts;
    }

    if (!path1.empty() && path1[0] == '/') {
        result = "/" + result;
    }

    return result;
}

file_manager::file_manager() : mutex(true) {
    seq_generator = 0;

#if !defined(NDEBUG)
    qlog::utility::util::log_device_console(qlog::LogLevel::info, "file_manager is constructed");
    qlog::utility::util::log_device_console(qlog::LogLevel::info, "base dir type 0: %s",
                                            get_base_dir(0).c_str());

    qlog::utility::util::log_device_console(qlog::LogLevel::info, "base dir type 1: %s",
                                            get_base_dir(1).c_str());
#endif
}

file_manager& file_manager::instance() {
    return *common_global_vars::get().file_manager_inst_;
}

std::string file_manager::get_base_dir(std::int32_t base_dir_type) {
    return qlog::platform::get_base_dir(base_dir_type);
}

file_manager::file_descriptor::file_descriptor() {
    handle_ptr = nullptr;
    seq = 0;
    ref_cout = 0;  // cout相当于 count吧 这是命名错误？
}

bool file_manager::file_descriptor::is_empty() const {
    return (!handle_ptr || !qlog::platform::is_platform_handle_valid(*handle_ptr)) && seq == 0;
}

void file_manager::file_descriptor::inc_ref() {
    ref_cout.add_fetch_seq_cst(1);
}

void file_manager::file_descriptor::clear() {
    qlog::platform::close_file(*handle_ptr);
    std::free(handle_ptr);

    handle_ptr = nullptr;
    seq = 0;
    ref_cout = 0;
    file_path.clear();
}

void file_manager::file_descriptor::dec_ref() {
    ref_cout.add_fetch_seq_cst(-1);

    assert(ref_cout.load() >= 0 && "file_descriptor ref_cout < 0");

    if (ref_cout.load() == 0) {
        clear();
    }
}

std::int32_t file_manager::get_file_descriptor_index_by_handle(const file_handle& handle) const {
    qlog::platform::scoped_mutex lock(
        const_cast<qlog::platform::mutex&>(mutex));  // 为什么需要 constcast

    if ((decltype(file_descriptors)::size_type)handle.idx_ >= file_descriptors.size()) {
        return -1;
    }

    if (handle.seq_ != file_descriptors[handle.idx_].seq) {
        return -1;
    }

    return static_cast<std::int32_t>(handle.idx_);
}

void file_manager::inc_ref(const file_handle& handle) {
    qlog::platform::scoped_mutex lock(const_cast<qlog::platform::mutex&>(mutex));

    std::int32_t desc_idx = get_file_descriptor_index_by_handle(handle);

    if (desc_idx < 0) {
        return;
    }

    auto& desc = file_descriptors[desc_idx];
    desc.inc_ref();
}

bool file_manager::close_file(file_handle& handle) {
    if (!handle) {
        return false;
    }

    qlog::platform::scoped_mutex lock(mutex);
    std::int32_t desc_idx = get_file_descriptor_index_by_handle(handle);

    if (desc_idx < 0) {
        return false;
    }

    auto& desc = file_descriptors[desc_idx];

#ifndef NDEBUG
    qlog::utility::util::log_device_console(
        qlog::LogLevel::info, "close file:%s idx:%" PRId32 ", seq:%" PRIu32 ", ref_count:%" PRId32,
        desc.file_path.c_str(), desc_idx, handle.seq_, desc.ref_cout.load() - 1);
#endif

    desc.dec_ref();
    handle.clear();

    return true;
}

bool file_manager::is_handle_valid(const file_handle& handle) const {
    if (!handle) {
        return false;
    }

    std::int32_t desc_idx = get_file_descriptor_index_by_handle(handle);
    return desc_idx >= 0;
}

void file_handle::clear() {
    idx_ = invalid_idx;
    seq_ = 0;
    handle_ptr_ = nullptr;
    file_path_.clear();
}

file_handle::file_handle() {
    clear();
}

file_handle::file_handle(const file_handle& rhs) {
    idx_ = rhs.idx_;
    seq_ = rhs.seq_;
    handle_ptr_ = rhs.handle_ptr_;
    file_path_ = rhs.file_path_;

    file_manager::instance().inc_ref(rhs);
}

file_handle::file_handle(file_handle&& rhs) noexcept {
    idx_ = rhs.idx_;
    seq_ = rhs.seq_;
    handle_ptr_ = rhs.handle_ptr_;
    file_path_ = std::move(rhs.file_path_);

    rhs.clear();
}

file_handle::~file_handle() {
    file_manager::instance().close_file(*this);
}

file_handle& file_handle::operator=(const file_handle& rhs) {
    idx_ = rhs.idx_;
    seq_ = rhs.seq_;
    handle_ptr_ = rhs.handle_ptr_;
    file_path_ = rhs.file_path_;

    file_manager::instance().inc_ref(rhs);
    return *this;
}

file_handle& file_handle::operator=(file_handle&& rhs) noexcept {
    file_manager::instance().close_file(*this);

    idx_ = rhs.idx_;
    seq_ = rhs.seq_;
    handle_ptr_ = rhs.handle_ptr_;
    file_path_ = std::move(rhs.file_path_);

    rhs.clear();
    return *this;
}

void file_handle::invalid() {
    file_manager::instance().close_file(*this);
    clear();
}

bool file_handle::is_valid() const {
    return file_manager::instance().is_handle_valid(*this);
}

std::string file_manager::get_lexically_path(const std::string& original_path) {
    return qlog::platform::get_lexically_path(original_path);
}

bool file_manager::is_absolute(const std::string& path) {
    return qlog::platform::is_absolute(path);
}

std::string file_manager::trans_process_relative_path_to_absolute_path(
    const std::string& relative_path, std::int32_t base_dir_type) {
    std::string absolute_path = relative_path;

    if (!is_absolute(absolute_path)) {
        absolute_path = combine_path(get_base_dir(base_dir_type), relative_path);
    }

    absolute_path = get_lexically_path(absolute_path);
    return absolute_path;
}

std::string file_manager::trans_process_absolute_path_to_relative_path(
    const std::string& absolute_path, std::int32_t base_dir_type) {
    if (!is_absolute(absolute_path)) {
        return absolute_path;
    }

    std::string relative_path = get_base_dir(base_dir_type);
    relative_path = get_lexically_path(relative_path);

    const std::string original_path = get_lexically_path(absolute_path);

    assert(!relative_path.empty() && "replace from string must no be empty!");

    std::string result;
    result.reserve(original_path.capacity());

    std::string::size_type pos = 0;

    while (pos < original_path.size()) {
        auto found_pos = original_path.find(relative_path, pos);

        if (found_pos != std::string::npos) {
            result.append(original_path, pos, found_pos - pos);
            pos = found_pos + relative_path.size();
        } else {
            result.append(original_path, pos, original_path.size() - pos);

            pos = original_path.size();
        }
    }

    return result;
}

bool file_manager::create_directory(const std::string& path) {
    if (is_dir(path.c_str())) {
        return true;
    }

    std::int32_t error_code = platform::make_dir(path.c_str());

    if (error_code != 0) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::error, "create_directory failed, directory:%s, error code:%" PRId32,
            path.c_str(), error_code);

        return false;
    }

    return true;
}

bool file_manager::is_dir(const std::string& path) {
    std::string real_path = get_lexically_path(path);

    if (!real_path.empty()) {
        if (real_path[real_path.size() - 1] != '/' && real_path[real_path.size() - 1] != '\\') {
            real_path += "/";
        }
    }

    return qlog::platform::is_dir(real_path.c_str());  // real path就是string 为什么还需要 c str
}

bool file_manager::is_file(const std::string& path) {
    std::string real_path = get_lexically_path(path);
    return qlog::platform::is_regular_file(real_path.c_str());
}

bool file_manager::remove_file_or_dir(const std::string& path) {
    std::string real_path = get_lexically_path(path);

    if (!(is_file(real_path) || is_dir(real_path))) {
        return false;
    }

    std::int32_t error_code = qlog::platform::remove_dir_or_file(real_path.c_str());

    if (error_code != 0) {
        switch (error_code) {
            case EACCES:
                qlog::utility::util::log_device_console(
                    qlog::LogLevel::error, "remove_file_or_dir failed, directory:%s, error code:%s",
                    real_path.c_str(), "EACCES");
                break;

            default:
                qlog::utility::util::log_device_console(
                    qlog::LogLevel::error,
                    "remove_file_or_dir failed, directory:%s, error code:%" PRId32,
                    real_path.c_str(), error_code);
                break;
        }

        return false;
    }

    return true;
}

std::vector<std::string> file_manager::get_sub_dirs_and_files_name(const std::string& path) {
    std::string real_path = get_lexically_path(path);
    return platform::get_all_sub_names(real_path.c_str());
}

void file_manager::get_all_files(const std::string& path, std::vector<std::string>& out_list,
                                 bool recursion) {
    auto temp_files = file_manager::get_sub_dirs_and_files_name(path);

    for (auto& info : temp_files) {
        std::string curr_path = combine_path(path, info);

        if (file_manager::is_file(curr_path)) {
            out_list.push_back(curr_path);
        } else {
            out_list.push_back(curr_path + "/");

            if (recursion) {
                get_all_files(curr_path, out_list);
            }
        }
    }
}

std::uint64_t file_manager::get_file_last_modified_epoch_ms(const std::string& path) {
    return qlog::platform::get_file_last_modified_epoch_ms(path.c_str());
}

file_handle file_manager::open_file(const std::string& path, file_open_mode_enum open_mode) {
    std::string real_path = get_lexically_path(path);
    file_handle handle;
    std::int32_t first_empty_idx = -1;

    qlog::platform::scoped_mutex lock(mutex);

    for (decltype(file_descriptors)::size_type i = 0; i < file_descriptors.size(); ++i) {
        auto& desc = file_descriptors[i];

        if (first_empty_idx == -1 && desc.is_empty()) {
            first_empty_idx = (std::int32_t)i;
        }
    }

    std::string file_name = get_file_name_from_path(path);

    if ((std::int32_t)(open_mode & file_open_mode_enum::auto_create) &&
        file_name.size() < path.size()) {
        std::string dir = get_directory_from_path(real_path);

        if (!create_directory(dir)) {
            return handle;
        }
    }

    qlog::platform::platform_file_handle platform_handle;

    std::int32_t open_file_errno = qlog::platform::open_file(
        real_path.c_str(), (qlog::platform::file_open_mode_enum)open_mode, platform_handle);

    if (!qlog::platform::is_platform_handle_valid(platform_handle) || open_file_errno != 0) {
        file_manager_file_error_no_ = open_file_errno;

        qlog::utility::util::log_device_console(
            qlog::LogLevel::error, "open_or_create_file failed, file:\"%s\", error code:%" PRId32,
            real_path.c_str(), open_file_errno);

        return handle;
    }
    qlog::platform::seek_file(platform_handle, platform::file_seek_option::end, 0);

    platform::platform_file_handle* handle_ptr =
        (platform::platform_file_handle*)std::malloc(sizeof(platform::platform_file_handle));

    *handle_ptr = platform_handle;

    handle.handle_ptr_ = handle_ptr;
    handle.file_path_ = real_path;

    if (first_empty_idx >= 0) {
        handle.idx_ = static_cast<std::uint32_t>(first_empty_idx);
    } else {
        file_descriptors.emplace_back(file_descriptor());
        handle.idx_ = static_cast<std::uint32_t>(file_descriptors.size() - 1);
    }

    auto& desc = file_descriptors[handle.idx_];
    assert(desc.is_empty());

    desc.seq = ++seq_generator;
    desc.handle_ptr = handle_ptr;
    desc.file_path = real_path;
    desc.inc_ref();

    handle.seq_ = file_descriptors[handle.idx_].seq;

#ifndef NDEBUG
    qlog::utility::util::log_device_console(
        qlog::LogLevel::info,
        "open  file:%s success, idx:%" PRIu32 ", seq:%" PRIu32 ", counter:%" PRId32,
        real_path.c_str(), handle.idx_, handle.seq_, desc.ref_cout.load());
#endif

    return handle;
}

bool file_manager::seek(const file_handle& handle, seek_option opt, std::int64_t offset) {
    if (seek_option::current == opt && offset == 0) {
        return true;
    }

    auto result = qlog::platform::seek_file(handle.platform_handle(),
                                            (qlog::platform::file_seek_option)opt, offset);

    if (result != 0) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::error,
            "seek(seek_option::current) file failed, errno:%" PRId32 ", file:%s, offset:%" PRId64,
            result, handle.file_path_.c_str(), offset);

        return false;
    }

    return true;
}

std::size_t file_manager::write_file(const file_handle& handle, const void* data,
                                     std::size_t length, seek_option opt,
                                     std::int64_t seek_offset) {
    if (!data || length == 0) {
        return 0;
    }

    if (!seek(handle, opt, seek_offset)) {
        return 0;
    }

    std::size_t total_written_count = 0;

    std::int32_t error_code =
        qlog::platform::write_file(handle.platform_handle(), data, length, total_written_count);

    if (error_code != 0) {
        file_manager_file_error_no_ = error_code;

        qlog::utility::util::log_device_console(
            qlog::LogLevel::error,
            "write file failed, errno:%" PRId32 ", path:%s, need_write_count:%" PRIu64
            ", real_write_count:%" PRIu64,
            error_code, handle.file_path_.c_str(), static_cast<std::uint64_t>(length),
            static_cast<std::uint64_t>(total_written_count));

        return total_written_count;
    }

    return total_written_count;
}

std::size_t file_manager::read_file(const file_handle& handle, void* dest_data, std::size_t length,
                                    seek_option opt, std::int64_t seek_offset) {
    if (!seek(handle, opt, seek_offset)) {
        return 0;
    }

    std::int32_t max_try_count = 10;
    std::size_t total_read_count = 0;

    while (max_try_count-- > 0 && total_read_count < length) {
        std::size_t need_read_count = length - total_read_count;
        std::size_t tmp_count;

        std::int32_t error_code = qlog::platform::read_file(
            handle.platform_handle(), (std::uint8_t*)dest_data + total_read_count, need_read_count,
            tmp_count);

        if (error_code != 0) {
            file_manager_file_error_no_ = error_code;

            qlog::utility::util::log_device_console(
                qlog::LogLevel::error,
                "read file failed, errno:%" PRId32 ", path:%s, need_read_count:%" PRIu64
                ", real_read_count:%" PRIu64,
                error_code, handle.file_path_.c_str(), static_cast<std::uint64_t>(length),
                static_cast<std::uint64_t>(total_read_count));

            break;
        }

        assert(tmp_count <= need_read_count && "read_count <= length");
        total_read_count += tmp_count;
    }

    return total_read_count;
}

bool file_manager::truncate_file(const file_handle& handle, std::size_t offset) {
    auto truncate_errno = platform::truncate_file(handle.platform_handle(), offset);

    if (truncate_errno != 0) {
        file_manager_file_error_no_ = truncate_errno;

        qlog::utility::util::log_device_console(
            qlog::LogLevel::error, "truncate file failed, file:\"%s\", error code:%" PRId32,
            handle.file_path_.c_str(), truncate_errno);

        return false;
    }

    std::size_t current_file_size = 0;

    std::int32_t get_file_size_errno =
        platform::get_file_size(handle.platform_handle(), current_file_size);

    if (get_file_size_errno != 0) {
        file_manager_file_error_no_ = get_file_size_errno;

        qlog::utility::util::log_device_console(
            qlog::LogLevel::error,
            "truncate file size check failed, file:\"%s\", error code:%" PRId32,
            handle.file_path_.c_str(), get_file_size_errno);

        return false;
    }

    if (current_file_size != offset) {
        file_manager_file_error_no_ = EIO;

        qlog::utility::util::log_device_console(
            qlog::LogLevel::error,
            "truncate file size mismatch, file:\"%s\", expected:%" PRIu64 ", actual:%" PRIu64,
            handle.file_path_.c_str(), static_cast<std::uint64_t>(offset),
            static_cast<std::uint64_t>(current_file_size));

        return false;
    }

    if (!seek(handle, seek_option::end, 0)) {
        return false;
    }

    return true;
}

std::string file_manager::get_file_absolute_path(const file_handle& handle) {
    qlog::platform::scoped_mutex lock(mutex);

    std::int32_t desc_idx = get_file_descriptor_index_by_handle(handle);

    if (desc_idx < 0) {
        return std::string();
    }

    return file_descriptors[desc_idx].file_path;
}

std::size_t file_manager::get_file_size(const file_handle& handle) const {
    qlog::platform::flush_file(handle.platform_handle());

    std::size_t current_file_size;

    std::int32_t err_code =
        qlog::platform::get_file_size(handle.platform_handle(), current_file_size);

    if (err_code == 0) {
        return current_file_size;
    }

    file_manager_file_error_no_ = err_code;

    switch (err_code) {
        case ENOENT:
            qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                    "get_file_size file not found: %s",
                                                    handle.file_path_.c_str());
            break;

        case EACCES:
            qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                    "get_file_size permission is denied for: %s",
                                                    handle.file_path_.c_str());
            break;

        case EISDIR:
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error, "get_file_size path is dir: %s", handle.file_path_.c_str());
            break;

        case ENOTDIR:
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error, "get_file_size path invalid: %s", handle.file_path_.c_str());
            break;

        default:
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error, "get_file_size path failed: %s, error code:%" PRId32,
                handle.file_path_.c_str(), err_code);
            break;
    }

    return 0;
}

bool file_manager::lock_file(const file_handle& handle) {
    return qlog::platform::lock_file(handle.platform_handle());
}

std::string file_manager::read_text(const file_handle& handle) {
    std::size_t size = get_file_size(handle);

    if (size == 0) {
        return "";
    }

    seek(handle, seek_option::begin, 0);

    std::string content(size, '\0');

    auto real_read_size = read_file(handle, (std::string::value_type*)content.data(), size);

    if (real_read_size < size) {
        content.resize(real_read_size);
    }

    return content;
}

bool file_manager::flush_file(const file_handle& file) {
    std::int32_t err_code = platform::flush_file(file.platform_handle());

    if (err_code == 0) {
        return true;
    }

    file_manager_file_error_no_ = err_code;

    qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                            "flush file failed, errno:%" PRId32 ",path:%s",
                                            err_code, file.file_path_.c_str());

    return false;
}

bool file_manager::flush_all_opened_files() {
    qlog::platform::scoped_mutex lock(mutex);
    bool result = true;

    for (decltype(file_descriptors)::size_type i = 0; i < file_descriptors.size(); ++i) {
        auto& desc = file_descriptors[i];

        if (!desc.is_empty()) {
            std::int32_t err_code = platform::flush_file(*desc.handle_ptr);

            if (err_code == 0) {
                continue;
            }

            file_manager_file_error_no_ = err_code;

            qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                    "flush file failed, errno:%" PRId32 ", path:%s",
                                                    err_code, desc.file_path.c_str());

            result = false;
        }
    }

    return result;
}

std::size_t file_manager::get_file_size(const std::string& path) {
    if (is_file(path) == false) {
        return 0;
    }

    auto file_handle = file_manager::instance().open_file(path, file_open_mode_enum::read);

    if (!file_handle) {
        return 0;
    }

    return file_manager::instance().get_file_size(file_handle);
}

std::string file_manager::read_all_text(const std::string& path) {
    auto file_handle = file_manager::instance().open_file(path, file_open_mode_enum::read);

    if (!file_handle.is_valid()) {
        return "";
    }

    std::string text = file_manager::instance().read_text(file_handle);

    return text;
}

void file_manager::append_all_text(const std::string& path, std::string content) {
    auto& file_manager_inst = file_manager::instance();

    auto file_handle = file_manager_inst.open_file(
        path, file_open_mode_enum::auto_create | file_open_mode_enum::read_write);

    if (!file_handle.is_valid()) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "failed to load file %s",
                                                path.c_str());

        return;
    }

    file_manager_inst.write_file(file_handle, content.data(), content.size());

    file_manager_inst.flush_file(file_handle);
}

void file_manager::write_all_text(const std::string& path, std::string content) {
    auto& file_manager_inst = file_manager::instance();

    auto file_handle = file_manager_inst.open_file(
        path, file_open_mode_enum::auto_create | file_open_mode_enum::read_write);

    if (!file_handle.is_valid()) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error, "failed to load file %s",
                                                path.c_str());

        return;
    }

    file_manager_inst.truncate_file(file_handle, 0);

    file_manager_inst.write_file(file_handle, content.data(), content.size());

    file_manager_inst.flush_file(file_handle);
}

bool file_manager::copy_file(const std::string& src_path, const std::string& dest_path) {
    auto src_file = file_manager::get_lexically_path(src_path);
    auto dest_file = file_manager::get_lexically_path(dest_path);

    if (src_file == dest_file) {
        return false;
    }

    auto fill_size = file_manager::instance().get_file_size(src_file);

    auto file_r = file_manager::instance().open_file(src_file, file_open_mode_enum::read);

    if (!file_r.is_valid()) {
        return false;
    }

    file_manager::instance().seek(file_r, file_manager::seek_option::begin, 0);

    auto file_w = file_manager::instance().open_file(
        dest_file, file_open_mode_enum::auto_create | file_open_mode_enum::write);

    if (!file_w.is_valid()) {
        return false;
    }

    file_manager::instance().truncate_file(file_w, 0);

    std::size_t write_size = 0;
    std::vector<std::uint8_t> cache_read_;
    std::size_t temp_size = 10 * 1024 * 1024;

    cache_read_.resize(temp_size);

    while (write_size < fill_size) {
        if (fill_size - write_size < temp_size) {
            temp_size = fill_size - write_size;
        }

        if (file_manager::instance().read_file(file_r, cache_read_.data(), temp_size) >=
            temp_size) {
            write_size += temp_size;

            file_manager::instance().write_file(file_w, cache_read_.data(), temp_size);
        }
    }

    file_manager::instance().close_file(file_r);
    file_manager::instance().close_file(file_w);

    return true;
}

std::int32_t file_manager::get_and_clear_last_file_error() {
    std::int32_t value = file_manager_file_error_no_;
    file_manager_file_error_no_ = 0;
    return value;
}

file_manager::~file_manager() {
    flush_all_opened_files();

#if !defined(QLOG_UNIT_TEST) && !defined(QLOG_TOOLS)
    qlog::utility::util::log_device_console(qlog::LogLevel::info, "file_manager is destructed");
#endif
}

}  // namespace qlog
