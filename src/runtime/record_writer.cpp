// Adapted from BQLog bq_log_api.cpp (Apache-2.0), committed reference 78cbfbef.
#include "qlog/runtime/record_writer.hpp"

#include "qlog/runtime/log_manager.hpp"
#include "qlog/utility/hash.hpp"
namespace qlog::runtime {
static constexpr std::uint8_t MAX_THREAD_NAME_LEN = 16;
struct log_api_thread_info_tls_type {
    qlog::platform::thread::thread_id thread_id_;
    std::uint8_t thread_name_len_;
    char thread_name_[MAX_THREAD_NAME_LEN];
};
QLOG_TLS log_api_thread_info_tls_type thread_info_tls_;
record_write_handle log_write_begin(std::uint64_t log_id, std::uint8_t log_level,
                                    std::uint32_t category_index, std::uint8_t format_string_type,
                                    std::uint32_t format_str_bytes_len, const void* format_str_data,
                                    std::uint32_t args_data_bytes_len) {
    auto log = log_manager::get_log_by_id(log_id);
    if (!log) {
        return {};
    }
    if (thread_info_tls_.thread_id_ == 0) {
        thread_info_tls_.thread_id_ = qlog::platform::thread::get_current_thread_id();
        std::string thread_name_tmp = qlog::platform::thread::get_current_thread_name();
        thread_info_tls_.thread_name_len_ = static_cast<std::uint8_t>(
            std::min(static_cast<std::size_t>(MAX_THREAD_NAME_LEN), thread_name_tmp.size()));
        if (thread_info_tls_.thread_name_len_ > 0) {
            std::memcpy(thread_info_tls_.thread_name_, thread_name_tmp.c_str(),
                        thread_info_tls_.thread_name_len_);
        }
    }
    auto length_without_ext_info = static_cast<std::uint32_t>(
        sizeof(qlog::record::RecordHeader) + qlog::utility::align_4(format_str_bytes_len) +
        args_data_bytes_len);
    auto ext_info_length = static_cast<std::uint32_t>(sizeof(qlog::record::RecordExtHeader) +
                                                      thread_info_tls_.thread_name_len_);
    auto total_length = length_without_ext_info + ext_info_length;
    auto epoch_ms = qlog::platform::high_performance_epoch_ms();
    record_write_handle handle;
    if (log->get_thread_mode() == log_thread_mode::sync) {
        handle.result = enum_buffer_result_code::success;
        handle.format_data_addr =
            log->get_sync_buffer(total_length) + sizeof(qlog::record::RecordHeader);
    } else {
        auto& log_buffer = log->get_buffer();
        auto write_handle = log_buffer.alloc_write_chunk(total_length, epoch_ms);
        bool need_awake_worker =
            write_handle.result == enum_buffer_result_code::err_not_enough_space ||
            write_handle.result == enum_buffer_result_code::err_wait_and_retry ||
            write_handle.low_space_flag;
        if (need_awake_worker) {
            auto& worker = log->get_thread_mode() == log_thread_mode::independent
                               ? log->get_worker()
                               : log_manager::instance().get_public_worker();
            worker.awake();
        }
        while (write_handle.result == enum_buffer_result_code::err_wait_and_retry) {
            qlog::platform::thread::cpu_relax();
            log_buffer.commit_write_chunk(write_handle);
            write_handle = log_buffer.alloc_write_chunk(total_length, epoch_ms);
        }
        handle.result = write_handle.result;
        if (write_handle.result != enum_buffer_result_code::success) {
            log_buffer.commit_write_chunk(write_handle);
            return handle;  // Do not form a pointer from a failed allocation's address.
        }
        handle.format_data_addr = write_handle.data_addr + sizeof(qlog::record::RecordHeader);
    }
    auto* head = reinterpret_cast<qlog::record::RecordHeader*>(handle.format_data_addr -
                                                               sizeof(qlog::record::RecordHeader));
#ifdef QLOG_DEBUG
    assert((reinterpret_cast<std::uintptr_t>(&head->timestamp_epoch) & 0x7) == 0 &&
           "log buffer alignment error");
#endif
    head->timestamp_epoch = epoch_ms;
    head->ext_info_offset = length_without_ext_info;
    head->level = log_level;
    head->category_idx = category_index;
    head->log_format_str_type = format_string_type;
    head->log_thread_id = thread_info_tls_.thread_id_;
    head->log_format_data_len = format_str_bytes_len;
    head->format_hash = 0;
    if (format_str_data) {
        head->format_hash = qlog::utility::qlog_memcpy_with_hash(
            handle.format_data_addr, format_str_data, format_str_bytes_len);
    }
    return handle;
}
void log_write_finish(std::uint64_t log_id, record_write_handle write_handle) {
    auto log = log_manager::get_log_by_id(log_id);
    if (!log) {
        assert(false && "commit invalid log buffer");
        return;
    }
    auto* chunk_data_ptr = write_handle.format_data_addr - sizeof(qlog::record::RecordHeader);
    auto* head = reinterpret_cast<qlog::record::RecordHeader*>(chunk_data_ptr);
    auto* ext_info =
        reinterpret_cast<qlog::record::RecordExtHeader*>(chunk_data_ptr + head->ext_info_offset);
    std::memcpy(&ext_info->thread_name_len_, &thread_info_tls_.thread_name_len_,
                sizeof(thread_info_tls_.thread_name_len_));
    if (thread_info_tls_.thread_name_len_ > 0) {
        std::memcpy(
            reinterpret_cast<std::uint8_t*>(ext_info) + sizeof(qlog::record::RecordExtHeader),
            thread_info_tls_.thread_name_, thread_info_tls_.thread_name_len_);
    }
    if (log->get_thread_mode() == log_thread_mode::sync) {
        log->sync_process(true);
    } else {
        qlog::buffer::log_buffer_write_handle handle;
        handle.data_addr = chunk_data_ptr;
        handle.result = write_handle.result;
        log->get_buffer().commit_write_chunk(handle);
    }
}
}  // namespace qlog::runtime
