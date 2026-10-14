#include "qlog/platform/file_io.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "qlog/global/common_vars.hpp"
#include "qlog/platform/file_path.hpp"
#include "qlog/platform/mutex.hpp"
#include "qlog/platform/thread_ops.hpp"
#include "qlog/utility/diagnostics.hpp"
#include "qlog/utility/hash.hpp"

namespace qlog::platform {

std::int32_t get_file_size(const platform_file_handle& file_handle, std::size_t& size_ref) {
    size_ref = 0;

    struct stat buf;                        // CR:stat是什么？
    if (::fstat(file_handle, &buf) != 0) {  // CR:fstat是什么呢？
        return errno;
    }

    if (!S_ISREG(buf.st_mode)) {
        return EISDIR;  // CR：返回的是什么 if判断的是什么 ？
    }

    size_ref = static_cast<std::size_t>(buf.st_size);
    return 0;
}

std::int32_t read_file(const platform_file_handle& file_handle, void* target_addr,
                       std::size_t read_size, std::size_t& out_real_read_size) {
    out_real_read_size = 0;

    const std::size_t max_size_pertime = static_cast<std::size_t>(SSIZE_MAX);
    char* current_target = static_cast<char*>(target_addr);

    while (out_real_read_size < read_size) {
        const std::size_t need_read_size_this_time =
            (std::min)(max_size_pertime, read_size - out_real_read_size);

        const ssize_t out_size = ::read(file_handle, current_target, need_read_size_this_time);

        if (out_size < 0) {
            return errno;
        }

        out_real_read_size += static_cast<std::size_t>(out_size);
        current_target += out_size;

        if (static_cast<std::size_t>(out_size) < need_read_size_this_time) {
            return 0;
        }  // CR:为什么outsize计算出来会比need少
    }
    return 0;
}

std::int32_t write_file(const platform_file_handle& file_handle, const void* src_addr,
                        std::size_t write_size, std::size_t& out_real_write_size) {
    out_real_write_size = 0;

    const char* current_src = static_cast<const char*>(src_addr);
    std::size_t remaining = write_size;

    while (remaining > 0) {
        const std::size_t chunk_size = (std::min)(remaining, static_cast<std::size_t>(SSIZE_MAX));

        const ssize_t written = ::write(file_handle, current_src, chunk_size);

        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return errno;
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
    int opt_platform = SEEK_CUR;

    switch (opt) {
        case file_seek_option::current:
            opt_platform = SEEK_CUR;
            break;  // CR:seek cur是什么意思

        case file_seek_option::begin:
            opt_platform = SEEK_SET;  // CR:seek set?
            break;

        case file_seek_option::end:
            opt_platform = SEEK_END;
            break;

        default:
            break;
    }

    if (::lseek(file_handle, static_cast<off_t>(offset), opt_platform) < 0) {
        return errno;  // CR:off_t ？ lseek是什么?
    }

    return 0;
}

std::int32_t truncate_file(const platform_file_handle& file_handle, std::size_t offset) {
    if (::ftruncate(file_handle, static_cast<off_t>(offset)) != 0) {
        return errno;
    }
    return 0;  // CR:这个函数是什么意思？
}

std::int32_t flush_file(const platform_file_handle& file_handle) {
#if defined(__APPLE__)
    if (::fsync(file_handle) != 0) {
        return errno;
    }
#else
    if (::fdatasync(file_handle) != 0) {
        return errno;
    }
#endif

    return 0;
}

bool lock_file(const platform_file_handle& file_handle) {
    if (!is_platform_handle_valid(file_handle)) {
        return false;
    }

    struct flock fl {};  // CR:flock是什么？
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    fl.l_pid = ::getpid();

    return ::fcntl(file_handle, F_SETLK, &fl) != -1;
}

bool unlock_file(const platform_file_handle& file_handle) {
    if (!is_platform_handle_valid(file_handle)) {
        return false;
    }

    struct flock fl {};
    fl.l_type = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start = 0;
    fl.l_len = 0;
    fl.l_pid = ::getpid();

    return ::fcntl(file_handle, F_SETLK, &fl) != -1;
}

std::uint64_t file_node_info::hash_code() const {
    std::uint8_t buf[sizeof(dev) + sizeof(ino)];  // dev和ino分别是什么？
    std::memcpy(buf, &dev, sizeof(dev));
    std::memcpy(buf + sizeof(dev), &ino, sizeof(ino));
    return qlog::utility::get_hash_64(buf, sizeof(buf));
}

class posix_dir_stack_holder {
   private:
    DIR* dp = nullptr;

   public:
    posix_dir_stack_holder(DIR* in_dp) : dp(in_dp) {}

    ~posix_dir_stack_holder() {
        if (dp) {
            closedir(dp);
        }
    }

    posix_dir_stack_holder(const posix_dir_stack_holder&) = delete;

    posix_dir_stack_holder& operator=(const posix_dir_stack_holder&) = delete;

    posix_dir_stack_holder(posix_dir_stack_holder&& rhs) noexcept : dp(rhs.dp) {
        rhs.dp = nullptr;
    }

    posix_dir_stack_holder& operator=(posix_dir_stack_holder&& rhs) noexcept {
        if (this != &rhs) {
            if (dp) {
                closedir(dp);
            }

            dp = rhs.dp;
            rhs.dp = nullptr;
        }

        return *this;
    }

    operator bool() const {
        return dp != nullptr;
    }

    DIR* get_dp() {
        return dp;
    }
};

inline posix_dir_stack_holder __posix_opendir(const char* path) {
    return posix_dir_stack_holder(opendir(path));
}

std::int32_t get_file_size(const char* file_path, std::size_t& size_ref) {
    struct stat buf;
    size_ref = 0;

    std::int32_t result = stat(file_path, &buf);

    if (result == 0) {
        if (!S_ISREG(buf.st_mode)) {
            return EISDIR;
        }

        size_ref = static_cast<std::size_t>(buf.st_size);
        return 0;
    }

    return errno;
}

bool is_dir(const char* path) {
    struct stat buf;
    std::int32_t result = stat(path, &buf);

    if (result == 0) {
        if (S_ISDIR(buf.st_mode)) {
            return true;
        }
    }

    return false;
}

bool is_regular_file(const char* path) {
    struct stat buf;
    std::int32_t result = stat(path, &buf);

    if (result == 0) {
        if (S_ISREG(buf.st_mode)) {
            return true;
        }
    }

    return false;
}

static std::int32_t make_dir_recursive(char* path) {
    if (std::strlen(path) == 0) {
        return 0;
    }

    if (is_dir(path)) {
        return 0;
    }

    char* ptr = std::strrchr(path, '/');

    if (ptr != nullptr) {
        *ptr = '\0';
        std::int32_t make_parent_result = make_dir_recursive(path);
        *ptr = '/';

        if (make_parent_result != 0) {
            return make_parent_result;
        }
    }

    if (mkdir(path, S_IRWXU | S_IRWXG | S_IRWXO) == 0) {
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

    char* path_cpy = strdup(path);
    std::int32_t result = make_dir_recursive(path_cpy);
    std::free(path_cpy);

    return result;
}
std::int32_t remove_dir_or_file_inner(std::string& path) {
    if (!is_dir(path.c_str())) {
        if (std::remove(path.c_str()) == 0) {
            return 0;
        }

        return errno;
    } else {
        std::size_t path_init_size = path.size();
        auto dp = __posix_opendir(path.c_str());

        if (!dp) {
            return errno;
        }

        dirent* dirp;

        while ((dirp = readdir(dp.get_dp())) != nullptr) {
            if (std::strcmp(dirp->d_name, ".") == 0 || std::strcmp(dirp->d_name, "..") == 0) {
                continue;
            }

            path.push_back('/');
            path += dirp->d_name;

            std::int32_t result = remove_dir_or_file_inner(path);
            path.resize(path_init_size);

            if (result != 0) {
                return result;
            }
        }

        if (std::remove(path.c_str()) == 0) {
            return 0;
        }

        return errno;
    }
}

std::int32_t remove_dir_or_file(const char* path) {
    std::string path_str = get_lexically_path(path);

    if (path_str.empty()) {
        return 0;
    }

    return remove_dir_or_file_inner(path_str);
}

static bool add_file_execlusive_check(const platform_file_handle& file_handle,
                                      file_open_mode_enum mode) {
    if (!(std::int32_t)(mode & (file_open_mode_enum::write | file_open_mode_enum::exclusive))) {
        return true;
    }

    if ((std::int32_t)(mode & file_open_mode_enum::exclusive)) {
        struct flock lock;
        std::memset(&lock, 0, sizeof(lock));

        lock.l_type = F_WRLCK;
        lock.l_whence = SEEK_SET;
        lock.l_start = 0;
        lock.l_len = 0;

        constexpr std::int32_t max_retry = 5;
        std::int32_t retry = 0;

        while (fcntl(file_handle, F_SETLK, &lock) == -1) {
            std::int32_t err = errno;

            if ((err == EAGAIN || err == EACCES || err == EINTR) && ++retry < max_retry) {
                qlog::platform::sleep(3);
                continue;
            }

            qlog::utility::util::log_device_console(
                qlog::LogLevel::error,
                "add_file_execlusive_check fcntl(F_SETLK) failed, "
                "fd:%" PRId32 ", errno:%" PRId32,
                file_handle, err);

            return false;
        }
    }

    struct stat file_info;

    if (fstat(file_handle, &file_info) < 0) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "add_file_execlusive_check fstat failed, "
                                                "fd:%" PRId32 ", error code:%" PRId32,
                                                file_handle, errno);

        return false;
    }

    auto& file_exclusive_cache = common_global_vars::get().file_exclusive_cache_;

    qlog::platform::scoped_mutex lock(common_global_vars::get().file_exclusive_mutex_);

    file_node_info node_info;
    node_info.dev = file_info.st_dev;
    node_info.ino = file_info.st_ino;

    auto iter = file_exclusive_cache.find(node_info);

    if (iter == file_exclusive_cache.end()) {
        file_exclusive_cache.add(node_info, mode);
        return true;
    } else {
        if ((std::int32_t)((iter->value() | mode) & file_open_mode_enum::exclusive)) {
            return false;
        }
    }

    return true;
}

static void remove_file_execlusive_check(const platform_file_handle& file_handle) {
    struct stat file_info;

    if (fstat(file_handle, &file_info) < 0) {
        qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                "remove_file_execlusive_check fstat failed, "
                                                "fd:%" PRId32 ", error code:%" PRId32,
                                                file_handle, errno);

        return;
    }

    auto& file_exclusive_cache = common_global_vars::get().file_exclusive_cache_;

    qlog::platform::scoped_mutex lock(common_global_vars::get().file_exclusive_mutex_);

    file_node_info node_info;
    node_info.dev = file_info.st_dev;
    node_info.ino = file_info.st_ino;

    file_exclusive_cache.erase(node_info);
}

std::int32_t open_file(const char* path, file_open_mode_enum mode,
                       platform_file_handle& out_file_handle) {
    std::int32_t flags = 0;

    if ((mode & file_open_mode_enum::read_write) == file_open_mode_enum::read_write) {
        flags |= O_RDWR;
    } else if ((std::int32_t)(mode & file_open_mode_enum::read)) {
        flags |= O_RDONLY;
    } else if ((std::int32_t)(mode & file_open_mode_enum::write)) {
        flags |= O_WRONLY;
    }

    if ((std::int32_t)(mode & file_open_mode_enum::auto_create)) {
        flags |= O_CREAT;
    }

    out_file_handle = open(path, flags, S_IRUSR | S_IWUSR | S_IRGRP);

    if (!is_platform_handle_valid(out_file_handle)) {
        return errno;
    }

    if (!add_file_execlusive_check(out_file_handle, mode)) {
        close(out_file_handle);
        out_file_handle = invalid_platform_file_handle;
        return EACCES;
    }

    return 0;
}

std::int32_t close_file(platform_file_handle& in_out_file_handle) {
    remove_file_execlusive_check(in_out_file_handle);

    auto result = close(in_out_file_handle);
    in_out_file_handle = invalid_platform_file_handle;

    if (result == 0) {
        return 0;
    }

    return errno;
}

std::uint64_t get_file_last_modified_epoch_ms(const char* path) {
    std::string abs_path = get_lexically_path(path);
    struct stat buf;

    std::int32_t result = stat(abs_path.c_str(), &buf);

    if (result == 0) {
        return (std::uint64_t)buf.st_mtime * 1000;
    }

    return (std::uint64_t)0;
}

std::vector<std::string> get_all_sub_names(const char* path) {
    std::vector<std::string> result;

    if (!path) {
        path = "./";
    }

    auto dp = __posix_opendir(path);

    if (!dp) {
        return result;
    }

    dirent* dirp;

    while ((dirp = readdir(dp.get_dp())) != nullptr) {
        if (std::strcmp(dirp->d_name, ".") == 0 || std::strcmp(dirp->d_name, "..") == 0) {
            continue;
        }

        const char* dp_name = dirp->d_name;
        result.push_back(dp_name);
    }

    return result;
}
}  // namespace qlog::platform
