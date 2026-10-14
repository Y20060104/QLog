#include "qlog/buffer/normal_buffer.hpp"

#include <algorithm>
#include <cinttypes>
#include <cstdint>
#include <cstring>

#include "qlog/platform/aligned_memory.hpp"
#include "qlog/utility/diagnostics.hpp"
#include "qlog/utility/utility_types.hpp"

namespace qlog::buffer {

create_memory_map_result normal_buffer::create_memory_map(std::size_t size,
                                                          const std::string& mmap_file_abs_path,
                                                          bool auto_create) {
    if (!qlog::memory_map::is_platform_support() || mmap_file_abs_path.empty()) {
        return create_memory_map_result::failed;
    }

    if (!auto_create) {
        if (!qlog::file_manager::is_file(mmap_file_abs_path)) {
            return create_memory_map_result::failed;
        }
    }

    auto create_opt = auto_create
                          ? (file_open_mode_enum::auto_create | file_open_mode_enum::read_write |
                             file_open_mode_enum::exclusive)
                          : (file_open_mode_enum::read_write | file_open_mode_enum::exclusive);

    if (auto_create || qlog::file_manager::is_file(mmap_file_abs_path)) {
        memory_map_file_ = qlog::file_manager::instance().open_file(mmap_file_abs_path, create_opt);
    }

    if (!memory_map_file_.is_valid()) {
        if (auto_create) {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::warning,
                "failed to open mmap file %s, use memory instead of mmap file, error code:%" PRId32,
                mmap_file_abs_path.c_str(), qlog::file_manager::get_and_clear_last_file_error());
        }
        return create_memory_map_result::failed;
    }

    std::size_t file_size = qlog::memory_map::get_min_size_of_memory_map_file(0, size);

    create_memory_map_result result = create_memory_map_result::failed;

    std::size_t current_file_size = qlog::file_manager::instance().get_file_size(memory_map_file_);

    if (current_file_size != file_size) {
        if (!qlog::file_manager::instance().truncate_file(memory_map_file_, file_size)) {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::warning,
                "normal buffer truncate memory map file \"%s\" failed, use memory instead.",
                memory_map_file_.abs_file_path().c_str());

            qlog::file_manager::instance().close_file(memory_map_file_);
            qlog::file_manager::remove_file_or_dir(mmap_file_abs_path);

            return create_memory_map_result::failed;
        }

        result = create_memory_map_result::new_created;
    } else {
        result = create_memory_map_result::use_existed;
    }

    memory_map_handle_ = qlog::memory_map::create_memory_map(memory_map_file_, 0, file_size);

    if (!memory_map_handle_.has_been_mapped()) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::warning,
            "ring buffer create memory map failed from file \"%s\" failed, use memory instead.",
            memory_map_file_.abs_file_path().c_str());

        qlog::file_manager::instance().close_file(memory_map_file_);
        qlog::file_manager::remove_file_or_dir(mmap_file_abs_path);

        return create_memory_map_result::failed;
    }

    if (((std::uintptr_t)memory_map_handle_.get_mapped_data() &
         (qlog::utility::QLOG_CACHE_LINE_SIZE - 1)) != 0) {
        qlog::utility::util::log_device_console(
            qlog::LogLevel::warning,
            "normal buffer memory map file \"%s\" memory address is not aligned, "
            "use memory instead.",
            memory_map_file_.abs_file_path().c_str());

        qlog::memory_map::release_memory_map(memory_map_handle_);
        qlog::file_manager::instance().close_file(memory_map_file_);
        qlog::file_manager::remove_file_or_dir(mmap_file_abs_path);

        return create_memory_map_result::failed;
    }

    buffer_data_ = memory_map_handle_.get_mapped_data();
    return result;
}

normal_buffer::normal_buffer(std::size_t size, const std::string& mmap_file_abs_path,
                             bool auto_create)
    : buffer_data_(nullptr), buffer_size_(0), delete_mmap_when_destruct_(false) {
    if (mmap_file_abs_path.empty()) {
        mmap_result_ = create_memory_map_result::failed;
    } else {
        mmap_result_ = create_memory_map(size, mmap_file_abs_path, auto_create);
    }

    if (mmap_result_ == create_memory_map_result::failed) {
        buffer_data_ = qlog::platform::aligned_alloc(qlog::utility::QLOG_CACHE_LINE_SIZE, size);

        if (buffer_data_ != nullptr) {
            buffer_size_ = size;
        }
    } else {
        buffer_data_ = memory_map_handle_.get_mapped_data();
        buffer_size_ = memory_map_handle_.get_mapped_size();
    }
}

normal_buffer::~normal_buffer() {
    if (memory_map_handle_.has_been_mapped()) {
        qlog::memory_map::release_memory_map(memory_map_handle_);

        std::string mmap_file_path = memory_map_file_.abs_file_path();

        qlog::file_manager::instance().close_file(memory_map_file_);

        if (delete_mmap_when_destruct_) {
            qlog::file_manager::remove_file_or_dir(mmap_file_path);
        }
    } else {
        qlog::platform::aligned_free(buffer_data_);
    }
}

void normal_buffer::resize(std::size_t new_size) {
    if (new_size == size()) {
        return;
    }

    bool is_from_mmap = is_memory_mapped();
    void* back_updata = nullptr;
    auto prev_size = size();

    if (is_from_mmap) {
        if (size() > 0) {
            back_updata =
                qlog::platform::aligned_alloc(qlog::utility::QLOG_CACHE_LINE_SIZE, prev_size);
            std::memcpy(back_updata, data(), size());
        }
    } else {
        back_updata = data();  // 为什么不来自mmap就直接赋值地址指针
    }

    if (is_from_mmap) {
        do {
            qlog::memory_map::release_memory_map(memory_map_handle_);

            new_size = qlog::memory_map::get_min_size_of_memory_map_file(0, new_size);

            if (!qlog::file_manager::instance().truncate_file(memory_map_file_, new_size)) {
                qlog::utility::util::log_device_console(
                    qlog::LogLevel::warning,
                    "normal buffer truncate memory map file \"%s\" failed, use memory instead.",
                    memory_map_file_.abs_file_path().c_str());

                std::string mmap_file_path = memory_map_file_.abs_file_path();

                qlog::file_manager::instance().close_file(memory_map_file_);
                qlog::file_manager::remove_file_or_dir(mmap_file_path);

                mmap_result_ = create_memory_map_result::failed;
                break;
            }
            memory_map_handle_ = qlog::memory_map::create_memory_map(memory_map_file_, 0, new_size);

            if (!memory_map_handle_.has_been_mapped()) {
                qlog::utility::util::log_device_console(
                    qlog::LogLevel::warning,
                    "ring buffer create memory map failed from file \"%s\" failed, "
                    "use memory instead.",
                    memory_map_file_.abs_file_path().c_str());

                std::string mmap_file_path = memory_map_file_.abs_file_path();

                qlog::file_manager::instance().close_file(memory_map_file_);
                qlog::file_manager::remove_file_or_dir(mmap_file_path);

                mmap_result_ = create_memory_map_result::failed;
            }

            buffer_data_ = memory_map_handle_.get_mapped_data();
            buffer_size_ = memory_map_handle_.get_mapped_size();
        } while (false);
    }

    if (!is_memory_mapped()) {
        buffer_data_ = qlog::platform::aligned_alloc(qlog::utility::QLOG_CACHE_LINE_SIZE, new_size);
        buffer_size_ = new_size;
    }

    if (back_updata != nullptr) {
        std::memcpy(buffer_data_, back_updata, (std::min)(prev_size, buffer_size_));
        qlog::platform::aligned_free(back_updata);
    }
}
// 为什么需要back update 直接创建大小为new size的不就好了?

}  // namespace qlog::buffer
