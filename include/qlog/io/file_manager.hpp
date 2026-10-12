#pragma once

#include <cassert>
#include <cerrno>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "qlog/platform/atomic.hpp"
#include "qlog/platform/file_defs.hpp"
#include "qlog/platform/file_io.hpp"
#include "qlog/platform/mutex.hpp"

namespace qlog {
struct common_global_vars;

struct file_handle {
    friend class file_manager;
    friend class memory_map;

   private:
    static constexpr std::uint32_t invalid_idx = 0xFFFFFFFF;

    std::uint32_t idx_;
    std::uint32_t seq_;
    qlog::platform::platform_file_handle* handle_ptr_;
    std::string file_path_;

   private:
    void clear();

   public:
    qlog_forceinline qlog::platform::platform_file_handle platform_handle() const {
        return *handle_ptr_;
    }

    file_handle();
    file_handle(const file_handle& rhs);
    file_handle(file_handle&& rhs) noexcept;  // CR:rhs是什么缩写 ？

    ~file_handle();

    file_handle& operator=(const file_handle& rhs);
    file_handle& operator=(file_handle&& rhs) noexcept;

    void invalid();
    bool is_valid() const;

    qlog_forceinline bool operator==(const file_handle& rhs) const {
        return idx_ == rhs.idx_ && seq_ == rhs.seq_;
    }

    qlog_forceinline operator bool() const {
        return idx_ != invalid_idx && seq_ > 0 &&
               qlog::platform::is_platform_handle_valid(platform_handle());
    }  // CR: operator 是bool？ 还是说 operator是返回类型？

    const std::string& abs_file_path() const {
        return file_path_;
    }
};

enum class file_open_mode_enum {
    read = static_cast<std::int32_t>(qlog::platform::file_open_mode_enum::read),
    write = static_cast<std::int32_t>(qlog::platform::file_open_mode_enum::write),
    read_write = static_cast<std::int32_t>(qlog::platform::file_open_mode_enum::read_write),
    auto_create = static_cast<std::int32_t>(qlog::platform::file_open_mode_enum::auto_create),
    exclusive = static_cast<std::int32_t>(qlog::platform::file_open_mode_enum::exclusive)
};  // CR: 不能直接复用吗？

qlog_forceinline file_open_mode_enum operator|(file_open_mode_enum lhs, file_open_mode_enum rhs) {
    return static_cast<file_open_mode_enum>(static_cast<std::int32_t>(lhs) |
                                            static_cast<std::int32_t>(rhs));
}

qlog_forceinline file_open_mode_enum operator&(file_open_mode_enum lhs, file_open_mode_enum rhs) {
    return static_cast<file_open_mode_enum>(static_cast<std::int32_t>(lhs) &
                                            static_cast<std::int32_t>(rhs));
}

class file_manager {
    friend file_handle;
    friend struct common_global_vars;  // CR:common global vars 是什么？

   private:
    struct file_descriptor {
        qlog::platform::platform_file_handle* handle_ptr;
        std::uint32_t seq;
        qlog::platform::atomic<std::int32_t> ref_cout;
        std::string file_path;

        file_descriptor();

        bool is_empty() const;
        void inc_ref();
        void dec_ref();
        void clear();
    };

   private:
    file_manager();
    ~file_manager();

   public:
    enum class seek_option : std::uint8_t {
        current = static_cast<std::uint8_t>(qlog::platform::file_seek_option::current),
        begin = static_cast<std::uint8_t>(qlog::platform::file_seek_option::begin),
        end = static_cast<std::uint8_t>(qlog::platform::file_seek_option::end)
    };

    static file_manager& instance();

    static std::string get_base_dir(std::int32_t base_dir_type);

    static std::string trans_process_absolute_path_to_relative_path(
        const std::string& absolute_path, std::int32_t base_dir_type);

    static std::string trans_process_relative_path_to_absolute_path(
        const std::string& relative_path, std::int32_t base_dir_type);

    static bool create_directory(const std::string& path);
    static std::string get_directory_from_path(const std::string& path);
    static std::string get_file_name_from_path(const std::string& path);

    static std::string combine_path(const std::string& path1, const std::string& path2);

    static std::string get_lexically_path(const std::string& original_path);
    static bool is_absolute(const std::string& path);
    static bool is_dir(const std::string& path);
    static bool is_file(const std::string& path);
    static bool remove_file_or_dir(const std::string& path);

    static std::vector<std::string> get_sub_dirs_and_files_name(const std::string& path);

    static void get_all_files(const std::string& path, std::vector<std::string>& out_list,
                              bool recursion = true);

    static std::uint64_t get_file_last_modified_epoch_ms(const std::string& path);

    static std::size_t get_file_size(const std::string& path);
    static std::string read_all_text(const std::string& path);

    static void append_all_text(const std::string& path, std::string content);

    static void write_all_text(const std::string& path, std::string content);

    static std::int32_t get_and_clear_last_file_error();

    bool is_handle_valid(const file_handle& handle) const;

    file_handle open_file(const std::string& path, file_open_mode_enum open_mode);

    std::size_t write_file(const file_handle& handle, const void* data, std::size_t length,
                           seek_option opt = seek_option::current, std::int64_t seek_offset = 0);

    bool copy_file(const std::string& src_path, const std::string& dest_path);

    bool truncate_file(const file_handle& handle, std::size_t offset);

    std::size_t read_file(const file_handle& handle, void* dest_data, std::size_t length,
                          seek_option opt = seek_option::current, std::int64_t seek_offset = 0);

    std::string get_file_absolute_path(const file_handle& handle);

    bool seek(const file_handle& handle, seek_option opt, std::int64_t offset);

    bool lock_file(const file_handle& handle);
    std::string read_text(const file_handle& handle);
    bool flush_file(const file_handle& file);
    bool flush_all_opened_files();
    bool close_file(file_handle& handle);

    std::size_t get_file_size(const file_handle& handle) const;

   private:
    std::int32_t get_file_descriptor_index_by_handle(const file_handle& handle) const;

    void inc_ref(const file_handle& handle);  // CR：为什么不需要dec？

   private:
    std::uint32_t seq_generator;
    std::vector<file_descriptor> file_descriptors;
    qlog::platform::mutex mutex;
};

#ifndef TO_ABSOLUTE_PATH
#define TO_ABSOLUTE_PATH(path, base_dir_type) \
    qlog::file_manager::trans_process_relative_path_to_absolute_path(path, base_dir_type)
#endif

#ifndef TO_RELATIVE_PATH
#define TO_RELATIVE_PATH(path, base_dir_type) \
    qlog::file_manager::trans_process_absolute_path_to_relative_path(path, base_dir_type)
#endif
// CR:idndef 是（TO_RELATIVE_PATH等）为0 才进行吗？

}  // namespace qlog