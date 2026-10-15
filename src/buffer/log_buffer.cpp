
#include "qlog/buffer/log_buffer.hpp"

#include <algorithm>
#include <cerrno>
#include <cinttypes>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

#include "qlog/global/log_vars.hpp"
#include "qlog/io/file_manager.hpp"
#include "qlog/platform/memory_map.hpp"

namespace qlog::buffer {

qlog_forceinline static void mark_block_removed(block_node_head* block, bool removed) {
    block->get_misc_data<log_buffer::block_misc_data>().is_removed().store_release(removed);
}

qlog_forceinline static bool is_block_removed(block_node_head* block) {
    if (block->get_misc_data<log_buffer::block_misc_data>().is_removed().load_raw()) {
        if (block->get_misc_data<log_buffer::block_misc_data>().is_removed().load_acquire()) {
            return true;
        }
    }
    return false;
}

qlog_forceinline static void mark_block_need_reallocate(block_node_head* block,
                                                        bool need_reallocate) {
    block->get_misc_data<log_buffer::block_misc_data>().need_reallocate_ = need_reallocate;
}

qlog_forceinline static bool is_block_need_reallocate(block_node_head* block) {
    return block->get_misc_data<log_buffer::block_misc_data>().need_reallocate_;
}

log_buffer::log_tls_buffer_info::~log_tls_buffer_info() {}

log_buffer::log_tls_info::~log_tls_info() {
    if (log_map_) {
        for (auto pair : *log_map_) {
            qlog::utility::shared_ptr<destruction_mark> destruction_protector =
                pair.value()->destruction_mark_;

            if (!destruction_protector.get()) {
                // 保留参考的空指针诊断分支。
                continue;
            }

            qlog::platform::scoped_spin_lock lock(destruction_protector->lock_);

            if (!destruction_protector->is_destructed_) {
                auto* buffer_info = pair.value();
                auto* log_buf = buffer_info->buffer_;

                if (log_buf) {
                    qlog::platform::scoped_spin_lock_write_crazy w_lock(
                        log_buf->temprorary_oversize_buffer_.array_lock_);

                    for (auto& os_buf : log_buf->temprorary_oversize_buffer_.buffers_array_) {
                        auto& ctx = os_buf->buffer_.get_misc_data<context_head>();

                        if (ctx.get_tls_info() == buffer_info &&
                            ctx.version_ == log_buf->get_version()) {
                            os_buf->is_thread_finished_ = true;
                        }
                    }
                }

                if (pair.value()->cur_block_) {
                    pair.value()
                        ->cur_block_->get_misc_data<log_buffer::block_misc_data>()
                        .context_.is_thread_finished_ = true;

                    mark_block_removed(pair.value()->cur_block_, true);
                } else {
                    while (true) {
                        auto finish_mark_handle =
                            pair.value()->buffer_->lp_buffer_.alloc_write_chunk(
                                sizeof(context_head));

                        scoped_log_buffer_handle<miso_ring_buffer> scoped_handle(
                            pair.value()->buffer_->lp_buffer_, finish_mark_handle);

                        if (finish_mark_handle.result == enum_buffer_result_code::success) {
                            auto* context =
                                reinterpret_cast<context_head*>(finish_mark_handle.data_addr);

                            context->version_ = pair.value()->buffer_->version_;
                            context->set_tls_info(pair.value());
                            context->is_thread_finished_ = true;
                            context->seq_ = pair.value()->wt_data_.current_write_seq_++;
                            context->is_external_ref_ = false;

                            break;
                        }
                    }
                }
            } else {
                qlog::utility::util::aligned_delete(pair.value());
            }
        }

        delete log_map_;
        log_map_ = nullptr;
    }

    cur_log_buffer_id_ = 0;
    cur_buffer_info_ = nullptr;
}

QLOG_TLS_NON_POD(log_buffer::log_tls_info, log_tls_info_)

/* qlog::platform::scoped_spin_lock_write_crazy w_lock(
                        log_buf->temprorary_oversize_buffer_.array_lock_);
                        arraylock是什么？*/
// is_thread_finished_ 代表了什么 为什么要判断if (ctx.get_tls_info() == buffer_info &&
//    ctx.version_ == log_buf->get_version())？

/*qlog_forceinline static void mark_block_need_reallocate(
    block_node_head* block,
    bool need_reallocate) {
    block->get_misc_data<log_buffer::block_misc_data>()
        .need_reallocate_ = need_reallocate;
}
        什么时候需要重新分配内存大小*/
// current_reading_.version_旧版本和新版本有什么不同

void log_buffer::prepare_and_fix_recovery_data() {
    for (std::uint32_t i = 0; i < MAX_RECOVERY_VERSION_RANGE; ++i) {
        rt_cache_.current_reading_.recovery_records_.push_back(
            decltype(rt_cache_.current_reading_.recovery_records_)::value_type());

#ifdef QLOG_UNIT_TEST
        rt_cache_.current_reading_.recovery_seq_records_.push_back(
            decltype(rt_cache_.current_reading_.recovery_seq_records_)::value_type());
#endif
    }

    lp_buffer_.set_thread_check_enable(false);

    lp_buffer_.data_traverse(
        [](std::uint8_t* data, std::uint32_t size, void* user_data) {
            (void)size;

            log_buffer& buffer = *reinterpret_cast<log_buffer*>(user_data);
            context_head& context = *reinterpret_cast<context_head*>(data);

            if (context.version_ == buffer.version_) {
#ifdef QLOG_UNIT_TEST
                qlog::utility::util::log_device_console(
                    qlog::LogLevel::error,
                    "fix invalid log data in lp_buffer when recovering, version:%" PRIu16
                    ", seq:%" PRIu32,
                    context.version_, context.seq_);
#endif
                context.version_++;
            } else if (static_cast<std::uint16_t>(buffer.version_ - 1 - context.version_) <
                       MAX_RECOVERY_VERSION_RANGE) {
                auto& recovery_map =
                    buffer.rt_cache_.current_reading_.recovery_records_[static_cast<std::uint16_t>(
                        buffer.version_ - 1 - context.version_)];

                auto iter = recovery_map.find(context.get_tls_info());

                if (iter == recovery_map.end()) {
                    recovery_map[context.get_tls_info()] = context.seq_;
                } else {
                    iter->value() = std::min(iter->value(), context.seq_);
                }

#ifdef QLOG_UNIT_TEST
                auto& seq_map = buffer.rt_cache_.current_reading_
                                    .recovery_seq_records_[static_cast<std::uint16_t>(
                                        buffer.version_ - 1 - context.version_)];

                auto seq_iter = seq_map.find(context.get_tls_info());

                if (seq_iter == seq_map.end()) {
                    seq_map[context.get_tls_info()] =
                        qlog::utility::hash_map<std::uint32_t, std::uint16_t>();
                }

                seq_map[context.get_tls_info()][context.seq_]++;
#endif
            }
        },
        this);

    lp_buffer_.set_thread_check_enable(true);

    auto group = hp_buffer_.first(group_list::lock_type::no_lock);

    while (group) {
        auto block = group.value().get_data_head().used_.first();

        while (block) {
            auto& context = block->get_misc_data<block_misc_data>().context_;

            if (context.version_ == version_) {
#ifdef QLOG_UNIT_TEST
                qlog::utility::util::log_device_console(
                    qlog::LogLevel::error,
                    "fix invalid log data in hp_buffer used list when recovering, "
                    "version:%" PRIu16 ", seq:%" PRIu32,
                    context.version_, context.seq_);
#endif
                context.version_++;
            } else if (static_cast<std::uint16_t>(version_ - 1 - context.version_) <
                       MAX_RECOVERY_VERSION_RANGE) {
                auto& recovery_map =
                    rt_cache_.current_reading_.recovery_records_[static_cast<std::uint16_t>(
                        version_ - 1 - context.version_)];

                auto iter = recovery_map.find(context.get_tls_info());

                if (iter == recovery_map.end()) {
                    recovery_map[context.get_tls_info()] = context.seq_;
                } else {
                    iter->value() = std::min(iter->value(), context.seq_);
                }

#ifdef QLOG_UNIT_TEST
                auto& seq_map =
                    rt_cache_.current_reading_.recovery_seq_records_[static_cast<std::uint16_t>(
                        version_ - 1 - context.version_)];

                auto seq_iter = seq_map.find(context.get_tls_info());

                if (seq_iter == seq_map.end()) {
                    seq_map[context.get_tls_info()] =
                        qlog::utility::hash_map<std::uint32_t, std::uint16_t>();
                }

                seq_map[context.get_tls_info()][context.seq_]++;
#endif
            }

            block = group.value().get_data_head().used_.next(block);
        }

        block = group.value().get_data_head().stage_.first();

        while (block) {
            auto& context = block->get_misc_data<block_misc_data>().context_;

            if (context.version_ == version_) {
#ifdef QLOG_UNIT_TEST
                qlog::utility::util::log_device_console(
                    qlog::LogLevel::error,
                    "fix invalid log data in hp_buffer stage list when recovering, "
                    "version:%" PRIu16 ", seq:%" PRIu32,
                    context.version_, context.seq_);
#endif
                context.version_++;
            } else if (static_cast<std::uint16_t>(version_ - 1 - context.version_) <
                       MAX_RECOVERY_VERSION_RANGE) {
                auto& recovery_map =
                    rt_cache_.current_reading_.recovery_records_[static_cast<std::uint16_t>(
                        version_ - 1 - context.version_)];

                auto iter = recovery_map.find(context.get_tls_info());

                if (iter == recovery_map.end()) {
                    recovery_map[context.get_tls_info()] = context.seq_;
                } else {
                    iter->value() = std::min(iter->value(), context.seq_);
                }

#ifdef QLOG_UNIT_TEST
                auto& seq_map =
                    rt_cache_.current_reading_.recovery_seq_records_[static_cast<std::uint16_t>(
                        version_ - 1 - context.version_)];

                auto seq_iter = seq_map.find(context.get_tls_info());

                if (seq_iter == seq_map.end()) {
                    seq_map[context.get_tls_info()] =
                        qlog::utility::hash_map<std::uint32_t, std::uint16_t>();
                }

                seq_map[context.get_tls_info()][context.seq_]++;
#endif
            }

            block = group.value().get_data_head().stage_.next(block);
        }

        group = hp_buffer_.next(group, group_list::lock_type::no_lock);
    }

    std::string over_size_memory_map_folder =
        TO_ABSOLUTE_PATH("qlog_mmap/mmap_" + config_.log_name + "/os", 0);

    if (qlog::file_manager::is_dir(over_size_memory_map_folder)) {
        std::vector<std::string> sub_names =
            qlog::file_manager::get_sub_dirs_and_files_name(over_size_memory_map_folder);

        for (const std::string& file_name : sub_names) {
            std::string full_path =
                qlog::file_manager::combine_path(over_size_memory_map_folder, file_name);

            if (!file_name.ends_with(".mmap") || !file_name.starts_with(config_.log_name + "_")) {
                qlog::utility::util::log_device_console(
                    qlog::LogLevel::warning, "remove invalid mmap file:%s", full_path.c_str());

                qlog::file_manager::remove_file_or_dir(full_path);
                continue;
            }

            char* end_ptr = nullptr;
            errno = 0;

            std::string file_name_cpy = file_name.substr(config_.log_name.size() + 1);

            std::uint64_t u64_value = std::strtoull(file_name_cpy.c_str(), &end_ptr, 10);

            if (errno == ERANGE ||
                end_ptr != file_name_cpy.c_str() + (file_name_cpy.size() - std::strlen(".mmap"))) {
                qlog::utility::util::log_device_console(
                    qlog::LogLevel::warning, "remove invalid mmap file:%s", full_path.c_str());

                qlog::file_manager::remove_file_or_dir(full_path);
                continue;
            }

            std::size_t file_size = qlog::file_manager::get_file_size(full_path);

            std::uint32_t default_buffer_size =
                static_cast<std::uint32_t>(qlog::utility::QLOG_CACHE_LINE_SIZE);

            while (default_buffer_size < UINT32_MAX) {
                std::uint32_t map_size =
                    oversize_buffer::calculate_size_of_memory(default_buffer_size);

                std::size_t desired_file_size =
                    qlog::memory_map::get_min_size_of_memory_map_file(0, map_size);

                if (desired_file_size == file_size) {
                    break;
                }

                default_buffer_size <<= 1;
            }

            if (default_buffer_size == UINT32_MAX) {
                qlog::utility::util::log_device_console(
                    qlog::LogLevel::warning, "remove invalid mmap file:%s, invalid size:%" PRIu64,
                    full_path.c_str(), static_cast<std::uint64_t>(file_size));

                qlog::file_manager::remove_file_or_dir(full_path);
                continue;
            }

            if (u64_value >
                current_oversize_buffer_index_.load(qlog::platform::memory_order::relaxed)) {
                current_oversize_buffer_index_.store(u64_value,
                                                     qlog::platform::memory_order::seq_cst);
            }

            auto recovery_buffer =
                std::make_unique<oversize_buffer_obj_def>(default_buffer_size, full_path, false);

            const auto& oversize_buffer_context =
                recovery_buffer->buffer_.get_misc_data<context_head>();

            if (oversize_buffer_context.seq_ != UINT32_MAX ||
                oversize_buffer_context.is_external_ref_ != true ||
                !is_version_valid(oversize_buffer_context.version_)) {
                qlog::utility::util::log_device_console(
                    qlog::LogLevel::warning,
                    "remove invalid mmap file when recovery:%s, invalid context",
                    full_path.c_str());

                recovery_buffer.reset();
                qlog::file_manager::remove_file_or_dir(full_path);
            } else {
                temprorary_oversize_buffer_.buffers_array_.emplace_back(std::move(recovery_buffer));
            }
        }
    }

#ifdef QLOG_UNIT_TEST
    for (std::size_t i = 0; i < rt_cache_.current_reading_.recovery_records_.size(); ++i) {
        for (const auto& record : rt_cache_.current_reading_.recovery_records_[i]) {
            const auto& seq_map = rt_cache_.current_reading_.recovery_seq_records_[i][record.key()];

            qlog::utility::util::log_device_console(
                qlog::LogLevel::info,
                "\t\t tls_info addr: %p, min valid seq: %" PRIu32 ", max valid seq:%" PRIu64,
                record.key(), record.value(),
                static_cast<std::uint64_t>(record.value() + seq_map.size() - 1));

            auto cur_min_seq = record.value();
            auto left_count = static_cast<std::uint32_t>(seq_map.size());

            while (left_count > 0) {
                auto seq_iter = seq_map.find(cur_min_seq);

                if (seq_iter == seq_map.end()) {
                    qlog::utility::util::log_device_console(qlog::LogLevel::error,
                                                            "miss seq:%" PRIu32, cur_min_seq);
                } else if (seq_iter->value() != 1) {
                    qlog::utility::util::log_device_console(
                        qlog::LogLevel::error, "duplicate seq:%" PRIu32 ", count:%" PRIu16,
                        cur_min_seq, seq_iter->value());
                }

                --left_count;
                ++cur_min_seq;
            }
        }
    }
#endif
}

void log_buffer::clear_recovery_data() {
    std::string memory_map_folder = TO_ABSOLUTE_PATH("qlog_mmap/mmap_" + config_.log_name, 0);

    if (qlog::file_manager::is_dir(memory_map_folder)) {
        qlog::file_manager::remove_file_or_dir(memory_map_folder);
    }
}

log_buffer::log_buffer(log_buffer_config& config)
    : config_(config),
      hp_buffer_(config_, BLOCKS_PER_GROUP_NODE),
      lp_buffer_(config_),
      hp_buffer_max_alloc_size_(UINT32_MAX),
      version_(config_.need_recovery
                   ? ++lp_buffer_.get_mmap_misc_data<lp_buffer_head_misc>().saved_version_
                   : 0),
      destruction_mark_(qlog::utility::make_shared<destruction_mark>()),
      current_oversize_buffer_index_(0) {
    static qlog::platform::atomic<std::uint64_t> id_generator(0);

    id_ = id_generator.add_fetch_relaxed(1);

    const_cast<log_buffer_config&>(config_).default_buffer_size =
        std::max((std::uint32_t)(16 * qlog::utility::QLOG_CACHE_LINE_SIZE),
                 qlog::utility::roundup_pow_of_two(config_.default_buffer_size));

    if (config.need_recovery) {
        rt_cache_.current_reading_.version_ =
            static_cast<std::uint16_t>(version_ - MAX_RECOVERY_VERSION_RANGE);

        prepare_and_fix_recovery_data();
    } else {
        assert(lp_buffer_.get_mmap_misc_data<lp_buffer_head_misc>().saved_version_ == 0 &&
               "invalid lp_buffer_head_misc init value");

        rt_cache_.current_reading_.version_ = 0;
        clear_recovery_data();
    }

    rt_cache_.current_reading_.is_in_recovery_reading_ =
        (rt_cache_.current_reading_.version_ != version_);

    block_node_head::alignment_assert();

    assert((QLOG_POD_RUNTIME_OFFSET_OF(log_tls_buffer_info, wt_data_) %
                qlog::utility::QLOG_CACHE_LINE_SIZE ==
            0) &&
           "log_tls_buffer_info current_write_seq_ must be 64 bytes aligned");

    assert((QLOG_POD_RUNTIME_OFFSET_OF(log_tls_buffer_info, rt_data_) %
                qlog::utility::QLOG_CACHE_LINE_SIZE ==
            0) &&
           "log_tls_buffer_info current_read_seq_ must be 64 bytes aligned");
}

log_buffer::~log_buffer() {
    qlog::platform::scoped_spin_lock lock(destruction_mark_->lock_);
    destruction_mark_->is_destructed_ = true;
}

block_node_head* log_buffer::alloc_new_hp_block() {
    log_tls_buffer_info& tls_buffer_info =
        log_tls_info__get_direct().get_buffer_info_directly(this);

    block_misc_data misc_data;
    std::memset(&misc_data, 0, sizeof(misc_data));

    misc_data.context_.set_tls_info(&tls_buffer_info);
    misc_data.context_.version_ = version_;
    misc_data.context_.is_thread_finished_ = false;
    misc_data.context_.seq_ = tls_buffer_info.wt_data_.current_write_seq_++;

    auto new_node = hp_buffer_.alloc_new_block(&misc_data, sizeof(block_misc_data));

    if (hp_buffer_max_alloc_size_ == UINT32_MAX) {
        hp_buffer_max_alloc_size_ = new_node->get_buffer().get_max_alloc_size();
    }

    return new_node;
}

log_buffer_write_handle log_buffer::wt_alloc_oversize_write_chunk(std::uint32_t size,
                                                               std::uint64_t current_epoch_ms) {
    auto& tls_buffer = log_tls_info__get_direct().get_buffer_info(this);

    auto& block_cache = tls_buffer.cur_block_;

    if (block_cache) {
        mark_block_removed(block_cache, true);
        block_cache = nullptr;
    }

    auto parent_result = lp_buffer_.alloc_write_chunk(sizeof(context_head));

    if (enum_buffer_result_code::success != parent_result.result) {
        if ((config_.policy == log_memory_policy::auto_expand_when_full ||
             config_.policy == log_memory_policy::block_when_full) &&
            parent_result.result == enum_buffer_result_code::err_not_enough_space) {
            parent_result.result = enum_buffer_result_code::err_wait_and_retry;
        }

        return parent_result;
    }

    auto* parent_context = reinterpret_cast<context_head*>(parent_result.data_addr);

    parent_context->version_ = version_;
    parent_context->is_thread_finished_ = false;
    parent_context->seq_ = tls_buffer.wt_data_.current_write_seq_++;
    parent_context->is_external_ref_ = true;
    parent_context->set_tls_info(&tls_buffer);

    decltype(parent_result) over_size_handle;

    {
        qlog::platform::scoped_spin_lock_read_crazy r_lock(temprorary_oversize_buffer_.array_lock_);

        for (auto& over_size_buffer : temprorary_oversize_buffer_.buffers_array_) {
            if (over_size_buffer->is_thread_finished_) {
                continue;
            }

            const auto& oversize_buffer_context =
                over_size_buffer->buffer_.get_misc_data<context_head>();

            if (oversize_buffer_context.version_ != parent_context->version_ ||
                oversize_buffer_context.get_tls_info() != parent_context->get_tls_info()) {
                continue;
            }

            over_size_buffer->buffer_lock_.read_lock();

            over_size_handle = over_size_buffer->buffer_.alloc_write_chunk(
                size + static_cast<std::uint32_t>(sizeof(context_head)));

            if (enum_buffer_result_code::success == over_size_handle.result) {
                over_size_buffer->last_used_epoch_ms_ = current_epoch_ms;

                tls_buffer.oversize_target_buffer_ = over_size_buffer.operator->();

                break;
            }

            over_size_buffer->buffer_lock_.read_unlock();
        }
    }

    if (enum_buffer_result_code::success != over_size_handle.result) {
        std::uint32_t default_buffer_size = (size < (UINT32_MAX >> 1)) ? (size << 1) : UINT32_MAX;

        std::string abs_recovery_file_path;

        if (config_.need_recovery) {
            auto new_index =
                current_oversize_buffer_index_.add_fetch(1, qlog::platform::memory_order::relaxed);

            char tmp[32];
            std::snprintf(tmp, sizeof(tmp), "_%" PRIu64, new_index);

            abs_recovery_file_path = TO_ABSOLUTE_PATH(
                "qlog_mmap/mmap_" + config_.log_name + "/os/" + config_.log_name + tmp + ".mmap",
                0);
        }

        qlog::platform::scoped_spin_lock_write_crazy w_lock(
            temprorary_oversize_buffer_.array_lock_);

        temprorary_oversize_buffer_.buffers_array_.emplace_back(
            std::make_unique<oversize_buffer_obj_def>(default_buffer_size, abs_recovery_file_path,
                                                      true));

        auto& new_buffer = *(temprorary_oversize_buffer_.buffers_array_.end() - 1);

        if (!new_buffer->buffer_.is_valid()) {
            temprorary_oversize_buffer_.buffers_array_.pop_back();
        } else {
            new_buffer->buffer_lock_.read_lock();

            auto& oversize_buffer_context = new_buffer->buffer_.get_misc_data<context_head>();

            oversize_buffer_context.version_ = version_;
            oversize_buffer_context.is_thread_finished_ = false;
            oversize_buffer_context.seq_ = UINT32_MAX;
            oversize_buffer_context.is_external_ref_ = true;
            oversize_buffer_context.set_tls_info(&tls_buffer);

            over_size_handle = new_buffer->buffer_.alloc_write_chunk(
                size + static_cast<std::uint32_t>(sizeof(context_head)));

            if (enum_buffer_result_code::success == over_size_handle.result) {
                new_buffer->last_used_epoch_ms_ = current_epoch_ms;

                tls_buffer.oversize_target_buffer_ = new_buffer.operator->();
            } else {
                new_buffer->buffer_lock_.read_unlock();
                temprorary_oversize_buffer_.buffers_array_.pop_back();
            }
        }
    }

    if (enum_buffer_result_code::success != over_size_handle.result) {
        --tls_buffer.wt_data_.current_write_seq_;

        parent_context->version_ = static_cast<std::uint16_t>(version_ + 1);

        lp_buffer_.commit_write_chunk(parent_result);

        log_buffer_write_handle fail;
        fail.result = enum_buffer_result_code::err_io_failure_drop;
        return fail;
    }

    auto* oversize_context = reinterpret_cast<context_head*>(over_size_handle.data_addr);

    oversize_context->version_ = version_;
    oversize_context->is_thread_finished_ = false;
    oversize_context->seq_ = parent_context->seq_;
    oversize_context->is_external_ref_ = true;
    oversize_context->set_tls_info(&tls_buffer);

    over_size_handle.data_addr += sizeof(context_head);

    tls_buffer.oversize_parent_handle_ = parent_result;

    return over_size_handle;
}

void log_buffer::wt_commit_oversize_write_chunk(const log_buffer_write_handle& oversize_handle) {
    auto& tls_buffer_info = log_tls_info__get_direct().get_buffer_info_directly(this);

    tls_buffer_info.oversize_target_buffer_->buffer_.commit_write_chunk(oversize_handle);

    tls_buffer_info.oversize_target_buffer_->buffer_lock_.read_unlock();

    tls_buffer_info.oversize_target_buffer_ = nullptr;

    lp_buffer_.commit_write_chunk(tls_buffer_info.oversize_parent_handle_);
}

log_buffer_write_handle log_buffer::alloc_write_chunk(std::uint32_t size,
                                                   std::uint64_t current_epoch_ms) {
    auto& tls_buffer = log_tls_info__get_direct().get_buffer_info(this);

    block_node_head*& block_cache = tls_buffer.cur_block_;

    std::uint64_t& thread_last_update_epoch_ms = tls_buffer.last_update_epoch_ms_;

    std::uint64_t& thread_update_times = tls_buffer.update_times_;

    bool is_high_frequency = (bool)block_cache;

    QLOG_UNLIKELY_IF(current_epoch_ms >=
                     thread_last_update_epoch_ms + HP_BUFFER_CALL_FREQUENCY_CHECK_INTERVAL) {
        if (thread_update_times < config_.high_frequency_threshold_per_second) {
            is_high_frequency = false;
        }

        thread_last_update_epoch_ms = current_epoch_ms;
        thread_update_times = 0;
    }

    if (++thread_update_times >= config_.high_frequency_threshold_per_second) {
        is_high_frequency = true;
        thread_last_update_epoch_ms = current_epoch_ms;
        thread_update_times = 0;
    }

    log_buffer_write_handle result;

    bool is_invalid_hp_size = is_high_frequency && (size > hp_buffer_max_alloc_size_);

    while (!is_invalid_hp_size) {
        if (is_high_frequency) {
            QLOG_UNLIKELY_IF(!block_cache) {
                block_cache = alloc_new_hp_block();
            }
            else QLOG_UNLIKELY_IF(is_block_need_reallocate(block_cache)) {
                mark_block_removed(block_cache, true);
                block_cache = alloc_new_hp_block();
            }

            result = block_cache->get_buffer().alloc_write_chunk(size);

            if (enum_buffer_result_code::err_not_enough_space == result.result) {
                switch (config_.policy) {
                    case log_memory_policy::auto_expand_when_full:
                        block_cache->get_buffer().commit_write_chunk(result);

                        mark_block_removed(block_cache, true);
                        block_cache = alloc_new_hp_block();
                        continue;
                        break;

                    case log_memory_policy::block_when_full:
                        result.result = enum_buffer_result_code::err_wait_and_retry;
                        break;

                    default:
                        break;
                }
            }

            break;
        } else {
            result = lp_buffer_.alloc_write_chunk(size +
                                                  static_cast<std::uint32_t>(sizeof(context_head)));

            if (enum_buffer_result_code::success == result.result) {
                auto* context = reinterpret_cast<context_head*>(result.data_addr);

                context->version_ = version_;
                context->is_thread_finished_ = false;
                context->seq_ = tls_buffer.wt_data_.current_write_seq_++;
                context->is_external_ref_ = false;
                context->set_tls_info(&tls_buffer);

                result.data_addr += sizeof(context_head);
            } else if (enum_buffer_result_code::err_not_enough_space == result.result) {
                switch (config_.policy) {
                    case log_memory_policy::auto_expand_when_full:
                        is_high_frequency = true;
                        thread_last_update_epoch_ms = current_epoch_ms;
                        thread_update_times = 0;

                        lp_buffer_.commit_write_chunk(result);
                        continue;
                        break;

                    case log_memory_policy::block_when_full:
                        result.result = enum_buffer_result_code::err_wait_and_retry;
                        break;

                    default:
                        break;
                }
            }

            if (block_cache) {
                mark_block_removed(block_cache, true);
                block_cache = nullptr;
            }

            break;
        }
    }

    QLOG_UNLIKELY_IF(
        (is_invalid_hp_size || enum_buffer_result_code::err_alloc_size_invalid == result.result) && size > 0) {
        return wt_alloc_oversize_write_chunk(size, current_epoch_ms);
    }

    return result;
}

void log_buffer::commit_write_chunk(const log_buffer_write_handle& handle) {
    auto& tls_buffer_info = log_tls_info__get_direct().get_buffer_info_directly(this);

    block_node_head*& block_cache = tls_buffer_info.cur_block_;

    bool is_high_frequency = (block_cache != nullptr);

    if (is_high_frequency) {
        block_cache->get_buffer().commit_write_chunk(handle);
    } else {
        if (handle.result == enum_buffer_result_code::success) {
            log_buffer_write_handle handle_copy = handle;

            handle_copy.data_addr -= sizeof(context_head);

            auto* context = reinterpret_cast<context_head*>(handle_copy.data_addr);

            bool is_external_ref_ =
                context->is_external_ref_ && handle.result == enum_buffer_result_code::success;

            QLOG_UNLIKELY_IF(is_external_ref_) {
                wt_commit_oversize_write_chunk(handle_copy);
            }
            else {
                lp_buffer_.commit_write_chunk(handle_copy);
            }
        } else {
            lp_buffer_.commit_write_chunk(handle);
        }
    }
}
// 地址减去 sizeof(context_head)什么有的要-有的不-？

#if defined(QLOG_DEBUG)
void log_buffer::debug_check_read_thread() {
    if (qlog::log_global_vars::get().is_thread_check_enabled()) {
        if (empty_thread_id_ == read_thread_id_) {
            read_thread_id_ = qlog::platform::get_current_thread_id();
        }

        qlog::platform::thread_id current_thread_id = qlog::platform::get_current_thread_id();

        assert(current_thread_id == read_thread_id_ &&
               "only single thread reading is supported for log_buffer!");
    }
}
#endif

log_buffer::context_verify_result log_buffer::verify_context(const context_head& context) {
    if (context.version_ == rt_cache_.current_reading_.version_) {
        QLOG_LIKELY_IF(context.version_ == version_) {
            auto expected_seq = context.get_tls_info()->rt_data_.current_read_seq_;

            if (context.seq_ == expected_seq) {
                return context_verify_result::valid;
            }

            assert((context.seq_ > expected_seq) && "seq_invalid only occurs when recovering");

            return context_verify_result::seq_pending;
        }
        else {
            auto iter =
                rt_cache_.current_reading_
                    .recovery_records_[static_cast<std::uint16_t>(version_ - 1 - context.version_)]
                    .find(context.get_tls_info());

#if defined(QLOG_DEBUG)
            assert(
                (iter !=
                 rt_cache_.current_reading_
                     .recovery_records_[static_cast<std::uint16_t>(version_ - 1 - context.version_)]
                     .end()) &&
                "unknowing thread found in recovering");
#endif

            auto expected_seq = iter->value();

            if (context.seq_ == expected_seq) {
                return context_verify_result::valid;
            }

            return context.seq_ < expected_seq ? context_verify_result::seq_invalid
                                               : context_verify_result::seq_pending;
        }
    } else if (!is_version_valid(context.version_)) {
        return context_verify_result::version_invalid;
    }

    return context_verify_result::version_pending;
}

log_buffer::context_verify_result log_buffer::verify_oversize_context(
    const log_buffer::context_head& parent_context,
    const log_buffer::context_head& oversize_context) {
    QLOG_LIKELY_IF(parent_context.version_ == oversize_context.version_) {
        if (parent_context.seq_ == oversize_context.seq_) {
            return context_verify_result::valid;
        } else if (parent_context.seq_ < oversize_context.seq_) {
            return context_verify_result::seq_pending;
        } else {
            assert(parent_context.version_ != version_ &&
                   "seq_invalid only occurs when recovering");

            return context_verify_result::seq_invalid;
        }
    }
    else if (parent_context.version_ < oversize_context.version_) {
        QLOG_UNLIKELY_IF(!is_version_valid(oversize_context.version_)) {
            return context_verify_result::version_invalid;
        }
        else {
            return context_verify_result::version_pending;
        }
    }
    else {
        return context_verify_result::version_invalid;
    }
}

void log_buffer::deregister_seq(const context_head& context) {
#if defined(QLOG_DEBUG)
    assert(context.version_ == rt_cache_.current_reading_.version_ && "invalid deregister_version");
#endif

    if (context.version_ == version_) {
        if (context.is_thread_finished_) {
            qlog::utility::util::aligned_delete(context.get_tls_info());
        } else {
            ++context.get_tls_info()->rt_data_.current_read_seq_;
        }
    } else {
        auto& recover_map =
            rt_cache_.current_reading_
                .recovery_records_[static_cast<std::uint16_t>(version_ - 1 - context.version_)];

#if defined(QLOG_DEBUG)
        assert((recover_map.find(context.get_tls_info()) != recover_map.end()) &&
               "unregister none exist seq");
#endif

        ++recover_map[context.get_tls_info()];

#if defined(QLOG_UNIT_TEST)
        // 参考单测在同一进程中模拟恢复，此时地址仍属于该进程。
        if (context.is_thread_finished_) {
            qlog::utility::util::aligned_delete(context.get_tls_info());
        }
#endif
    }

    refresh_traverse_end_mark();
}

void log_buffer::rt_try_traverse_to_next_version() {
    auto& rt_reading = rt_cache_.current_reading_;

    auto& recover_map =
        rt_cache_.current_reading_
            .recovery_records_[static_cast<std::uint16_t>(version_ - 1 - rt_reading.version_)];

    recover_map.clear();

#ifdef QLOG_UNIT_TEST
    rt_cache_.current_reading_
        .recovery_seq_records_[static_cast<std::uint16_t>(version_ - 1 - rt_reading.version_)]
        .clear();
#endif

    ++rt_reading.version_;

    rt_reading.is_in_recovery_reading_ = (rt_reading.version_ != version_);
}

void log_buffer::refresh_traverse_end_mark() {
    auto& rt_reading = rt_cache_.current_reading_;

    if (rt_cache_.mem_optimize_.is_block_marked_removed && rt_reading.cur_block_) {
        rt_reading.traverse_end_block_is_working_ = false;
    } else {
        rt_reading.traverse_end_block_is_working_ = true;
        rt_reading.traverse_end_block_ = rt_reading.cur_block_;
    }

    if (rt_reading.last_block_ == rt_reading.cur_block_) {
        assert(!rt_reading.last_block_);
    }
}

#if defined(QLOG_UNIT_TEST)
const log_buffer::log_tls_buffer_info& log_buffer::get_buffer_info_for_this_thread() const {
    return log_tls_info__get_direct().get_buffer_info(this);
}
#endif

bool log_buffer::rt_try_traverse_to_next_block_in_group(context_verify_result& out_verify_result) {
    auto& mem_opt = rt_cache_.mem_optimize_;
    auto& rt_reading = rt_cache_.current_reading_;

    bool is_cur_block_in_group =
        rt_reading.cur_group_.value().is_range_include(rt_reading.cur_block_);

    block_node_head* next_block =
        is_cur_block_in_group
            ? rt_reading.cur_group_.value().get_data_head().used_.next(rt_reading.cur_block_)
            : rt_reading.cur_group_.value().get_data_head().used_.first();

    if (!next_block) {
        next_block = rt_reading.cur_group_.value().get_data_head().stage_.pop();

        if (next_block) {
            rt_reading.cur_group_.value().get_data_head().used_.push_after_thread_unsafe(
                is_cur_block_in_group ? rt_reading.cur_block_ : nullptr, next_block);
        }
    }

    if (next_block) {
        assert(next_block != rt_reading.cur_block_);
    }

    bool block_removed = false;

#if defined(QLOG_DEBUG)
    if (is_cur_block_in_group) {
        assert(
            rt_reading.cur_group_.value().get_data_head().used_.is_include(rt_reading.cur_block_));
    }
#endif

    if (is_cur_block_in_group) {
        if (mem_opt.is_block_marked_removed) {
            if (context_verify_result::valid == mem_opt.verify_result) {
                const auto& context =
                    rt_reading.cur_block_->get_misc_data<block_misc_data>().context_;

                deregister_seq(context);
            }

            if (rt_reading.cur_group_.value().is_range_include(rt_reading.last_block_)) {
                hp_buffer_.recycle_block_thread_unsafe(
                    rt_reading.cur_group_, rt_reading.last_block_, rt_reading.cur_block_);
            } else {
                hp_buffer_.recycle_block_thread_unsafe(rt_reading.cur_group_, nullptr,
                                                       rt_reading.cur_block_);
            }

            block_removed = true;
        } else {
            ++mem_opt.cur_group_using_blocks_num_;
        }
    }

#if defined(QLOG_DEBUG)
    assert(mem_opt.cur_group_using_blocks_num_ <= hp_buffer_.get_max_block_count_per_group());
#endif

    if ((!block_removed && is_cur_block_in_group) || rt_reading.cur_block_ == nullptr) {
        rt_reading.last_block_ = rt_reading.cur_block_;
    }

    if (next_block) {
        auto& context = next_block->get_misc_data<block_misc_data>().context_;

        out_verify_result = verify_context(context);

        mem_opt.is_block_marked_removed =
            (out_verify_result == context_verify_result::valid &&
             (is_block_removed(next_block) || (context.version_ != version_))) ||
            out_verify_result == context_verify_result::version_invalid ||
            out_verify_result == context_verify_result::seq_invalid;

        mem_opt.verify_result = out_verify_result;

        if (mem_opt.left_holes_num_ > 0) {
            mark_block_need_reallocate(next_block, true);
            --mem_opt.left_holes_num_;
        }

        rt_reading.cur_block_ = next_block;
        return true;
    } else {
        if (block_removed) {
            rt_reading.cur_block_ = rt_reading.last_block_;
        }

        mem_opt.is_block_marked_removed = false;
        mem_opt.verify_result = context_verify_result::version_invalid;
    }

    return false;
}

bool log_buffer::rt_try_traverse_to_next_group() {
    auto& mem_opt = rt_cache_.mem_optimize_;
    auto& rt_reading = rt_cache_.current_reading_;

    auto next_group = rt_reading.cur_group_
                          ? hp_buffer_.next(rt_reading.cur_group_, group_list::lock_type::no_lock)
                          : hp_buffer_.first(group_list::lock_type::no_lock);

    bool group_removed = false;

    if (mem_opt.cur_group_using_blocks_num_ == 0 && rt_reading.cur_group_) {
        group_list::iterator current_lock_iterator;

        if (rt_reading.last_group_) {
            current_lock_iterator =
                hp_buffer_.next(rt_reading.last_group_, group_list::lock_type::write_lock);
        } else {
            while (current_lock_iterator != rt_reading.cur_group_) {
                current_lock_iterator =
                    static_cast<bool>(current_lock_iterator)
                        ? hp_buffer_.next(current_lock_iterator, group_list::lock_type::write_lock)
                        : hp_buffer_.first(group_list::lock_type::write_lock);

                if (!current_lock_iterator) {
                    assert(false && "try lock removing group from head failed");
                }
            }
        }

        assert(current_lock_iterator == rt_reading.cur_group_ && "try lock removing group failed");

        if ((!current_lock_iterator.value().get_data_head().used_.first()) &&
            (!current_lock_iterator.value().get_data_head().stage_.first())) {
#ifdef QLOG_UNIT_TEST
            std::uint16_t free_blocks = 0;

            auto free_block_iter = current_lock_iterator.value().get_data_head().free_.first();

            while (free_block_iter) {
                ++free_blocks;

                free_block_iter =
                    current_lock_iterator.value().get_data_head().free_.next(free_block_iter);
            }

            if (free_blocks != hp_buffer_.get_max_block_count_per_group()) {
                qlog::utility::util::log_device_console(qlog::LogLevel::error, "free list:");

                current_lock_iterator.value().get_data_head().free_.debug_output();

                qlog::utility::util::log_device_console(qlog::LogLevel::error, "used list:");

                current_lock_iterator.value().get_data_head().used_.debug_output();

                qlog::utility::util::log_device_console(qlog::LogLevel::error, "stage list:");

                current_lock_iterator.value().get_data_head().stage_.debug_output();

                assert(false && "all blocks should be free when group is removed");
            }
#endif

            group_removed = true;

            assert(!rt_reading.cur_group_.value().is_range_include(rt_reading.last_block_));

            hp_buffer_.delete_and_unlock_thread_unsafe(current_lock_iterator);
        } else {
            hp_buffer_.next(current_lock_iterator, group_list::lock_type::no_lock);
        }
    }

    if (rt_reading.cur_group_ && !group_removed) {
        mem_opt.left_holes_num_ += static_cast<std::uint32_t>(
            hp_buffer_.get_max_block_count_per_group() - mem_opt.cur_group_using_blocks_num_);
    }

    mem_opt.cur_group_using_blocks_num_ = 0;

    if (!group_removed) {
        rt_reading.last_group_ = rt_reading.cur_group_;
    }

    if (!next_group) {
        mem_opt.left_holes_num_ = 0;
        rt_reading.cur_block_ = nullptr;
        rt_reading.last_block_ = nullptr;
    }

    rt_reading.cur_group_ = next_group;
    return next_group;
}

QLOG_TLS_NON_POD(log_buffer_read_handle, rt_oversize_parent_handle_)
QLOG_TLS void* rt_oversize_target_buffer_;

bool log_buffer::rt_read_oversize_chunk(const log_buffer_read_handle& parent_handle,
                                        log_buffer_read_handle& out_oversize_handle) {
    rt_oversize_parent_handle_.get() = parent_handle;

    const auto& parent_context = *reinterpret_cast<const context_head*>(parent_handle.data_addr);

    qlog::platform::scoped_spin_lock_read_crazy r_lock(temprorary_oversize_buffer_.array_lock_);

    for (auto& buffer : temprorary_oversize_buffer_.buffers_array_) {
        const auto& oversize_buffer_context = buffer->buffer_.get_misc_data<context_head>();

        if (oversize_buffer_context.version_ != parent_context.version_ ||
            oversize_buffer_context.get_tls_info() != parent_context.get_tls_info()) {
            continue;
        }

        bool read_done = false;

        while (!read_done) {
            auto read_handle = buffer->buffer_.read_chunk();

            if (enum_buffer_result_code::success == read_handle.result) {
                const auto& oversize_context =
                    *reinterpret_cast<const context_head*>(read_handle.data_addr);

                auto verify_result = verify_oversize_context(parent_context, oversize_context);

                switch (verify_result) {
                    case log_buffer::context_verify_result::valid:
                        rt_oversize_target_buffer_ = buffer.operator->();

                        out_oversize_handle = read_handle;
                        return true;
                        break;

                    case log_buffer::context_verify_result::version_invalid:
                    case log_buffer::context_verify_result::seq_invalid:
                        buffer->buffer_.return_read_chunk(read_handle);
                        break;

                    case log_buffer::context_verify_result::version_pending:
                    case log_buffer::context_verify_result::seq_pending:
                        buffer->buffer_.discard_read_chunk(read_handle);

                        read_done = true;
                        break;

                    default:
                        assert(false &&
                               "unexpected context verify result in "
                               "rt_read_oversize_chunk");
                        break;
                }
            } else {
                buffer->buffer_.return_read_chunk(read_handle);
                read_done = true;
            }
        }
    }

    assert(parent_context.version_ != version_);

    qlog::utility::util::log_device_console(
        qlog::LogLevel::warning,
        "over size data losing detected in recovering log data, "
        "version:%" PRIu16 ", seq:%" PRIu32,
        parent_context.version_, parent_context.seq_);

    return false;
}

void log_buffer::rt_return_oversize_read_chunk(const log_buffer_read_handle& oversize_handle) {
    auto oversize_buffer = ((oversize_buffer_obj_def*)rt_oversize_target_buffer_);

    oversize_buffer->buffer_.return_read_chunk(oversize_handle);

    lp_buffer_.return_read_chunk(rt_oversize_parent_handle_.get());
}

void log_buffer::rt_recycle_oversize_buffers() {
    QLOG_LIKELY_IF(temprorary_oversize_buffer_.buffers_array_.empty()) {
        return;
    }

    auto current_epoch_ms = qlog::platform::high_performance_epoch_ms();

    bool need_recycle = false;

    {
        qlog::platform::scoped_try_spin_lock_read_crazy array_read_try_lock(
            temprorary_oversize_buffer_.array_lock_);

        if (array_read_try_lock.owns_lock()) {
            for (auto iter = temprorary_oversize_buffer_.buffers_array_.begin();
                 iter != temprorary_oversize_buffer_.buffers_array_.end(); ++iter) {
                auto& oversize_buffer = *iter;

                if (current_epoch_ms >=
                    oversize_buffer->last_used_epoch_ms_ + OVERSIZE_BUFFER_RECYCLE_INTERVAL_MS) {
                    need_recycle = true;
                    break;
                }
            }
        }
    }

    if (need_recycle) {
        qlog::platform::scoped_try_spin_lock_write_crazy array_write_try_lock(
            temprorary_oversize_buffer_.array_lock_);

        if (array_write_try_lock.owns_lock()) {
            for (std::size_t i = temprorary_oversize_buffer_.buffers_array_.size(); i > 0; --i) {
                auto index = i - 1;

                if (rt_oversize_target_buffer_ ==
                    temprorary_oversize_buffer_.buffers_array_[index].operator->()) {
                    continue;
                }

                auto& oversize_buffer = *temprorary_oversize_buffer_.buffers_array_[index];

                bool is_empty = false;

                {
                    qlog::platform::scoped_try_spin_lock_write_crazy buffer_write_try_lock(
                        oversize_buffer.buffer_lock_);

                    if (buffer_write_try_lock.owns_lock()) {
                        bool loop_finished = false;

                        while (!loop_finished) {
                            auto read_handle = oversize_buffer.buffer_.read_chunk();

                            if (read_handle.result == enum_buffer_result_code::err_empty_log_buffer) {
                                oversize_buffer.buffer_.return_read_chunk(read_handle);

                                is_empty = true;
                                loop_finished = true;
                                break;
                            } else if (read_handle.result == enum_buffer_result_code::success) {
                                const auto& context =
                                    *reinterpret_cast<const context_head*>(read_handle.data_addr);

                                auto verify_result = verify_context(context);

                                switch (verify_result) {
                                    case log_buffer::context_verify_result::valid:
                                    case log_buffer::context_verify_result::version_pending:
                                    case log_buffer::context_verify_result::seq_pending:
                                        oversize_buffer.buffer_.discard_read_chunk(read_handle);

                                        loop_finished = true;
                                        break;

                                    case log_buffer::context_verify_result::version_invalid:
                                    case log_buffer::context_verify_result::seq_invalid:
                                        oversize_buffer.buffer_.return_read_chunk(read_handle);
                                        break;

                                    default:
                                        assert(false &&
                                               "unexpected context verify "
                                               "result in "
                                               "rt_read_oversize_chunk");
                                        break;
                                }
                            } else {
                                assert(false && "impossible oversize buffer read result");
                                break;
                            }
                        }
                    }
                }

                if (is_empty) {
                    std::string abs_mmap_file_path =
                        temprorary_oversize_buffer_.buffers_array_[index]
                            ->buffer_.get_mmap_file_path();

                    // 对应参考 array::erase_replace 原有范围断言。
                    assert(index < temprorary_oversize_buffer_.buffers_array_.size() &&
                           "erase_replace param where_it must between "
                           "begin() and end() iterator!");

                    if (index != temprorary_oversize_buffer_.buffers_array_.size() - 1) {
                        temprorary_oversize_buffer_.buffers_array_[index] =
                            std::move(temprorary_oversize_buffer_.buffers_array_.back());
                    }

                    temprorary_oversize_buffer_.buffers_array_.pop_back();

                    if (qlog::file_manager::is_file(abs_mmap_file_path)) {
                        qlog::file_manager::remove_file_or_dir(abs_mmap_file_path);
                    }
                }
            }
        }
    }
}

bool log_buffer::rt_read_from_lp_buffer(log_buffer_read_handle& out_handle) {
    while (true) {
        auto read_handle = lp_buffer_.read_chunk();

        if (enum_buffer_result_code::success == read_handle.result) {
            const auto& context = *reinterpret_cast<const context_head*>(read_handle.data_addr);

            auto verify_result = verify_context(context);

            switch (verify_result) {
                case context_verify_result::valid:
                    QLOG_LIKELY_IF(!context.is_thread_finished_) {
                        QLOG_LIKELY_IF(!context.is_external_ref_) {
                            out_handle = read_handle;
                            return true;
                        }
                        else {
                            decltype(read_handle) oversize_handle;

                            if (rt_read_oversize_chunk(read_handle, oversize_handle)) {
                                out_handle = oversize_handle;
                                return true;
                            }
                        }
                    }

                    deregister_seq(context);
                    lp_buffer_.return_read_chunk(read_handle);
                    break;

                case context_verify_result::version_invalid:
                case context_verify_result::seq_invalid:
                    lp_buffer_.return_read_chunk(read_handle);
                    break;

                case context_verify_result::version_pending:
                case context_verify_result::seq_pending:
                    lp_buffer_.discard_read_chunk(read_handle);
                    return false;
                    break;

                default:
                    break;
            }
        } else {
            lp_buffer_.return_read_chunk(read_handle);
            return false;
        }
    }
}

log_buffer_read_handle log_buffer::read_chunk_full_impl() {
    auto& rt_reading = rt_cache_.current_reading_;

    log_buffer_read_handle read_handle;

    context_verify_result verify_result = context_verify_result::version_invalid;

    refresh_traverse_end_mark();

    bool loop_finished = false;

    while (!loop_finished) {
        switch (rt_reading.state_) {
            case read_state::lp_buffer_reading:
                if (rt_read_from_lp_buffer(read_handle)) {
                    loop_finished = true;

                    read_handle.data_addr += sizeof(context_head);
                    read_handle.data_size -= static_cast<std::uint32_t>(sizeof(context_head));

                    break;
                }

                rt_reading.state_ = read_state::next_group_finding;

                rt_recycle_oversize_buffers();
                break;

            case read_state::hp_block_reading:
                rt_reading.hp_handle_cache_ = rt_reading.cur_block_->get_buffer().batch_read();

                if (enum_buffer_result_code::success == rt_reading.hp_handle_cache_.result) {
                    read_handle = rt_reading.hp_handle_cache_.next();

                    loop_finished = true;
                    break;
                }

                rt_reading.cur_block_->get_buffer().return_batch_read_chunks(
                    rt_reading.hp_handle_cache_);

                rt_reading.state_ = read_state::next_block_finding;
                break;

            case read_state::next_block_finding: {
#if defined(QLOG_DEBUG)
                assert(rt_reading.cur_group_);
#endif

                const auto next_block_found = rt_try_traverse_to_next_block_in_group(verify_result);

                if (!next_block_found) {
                    rt_reading.state_ = read_state::next_group_finding;
                    break;
                }

#if defined(QLOG_DEBUG)
                assert(rt_reading.cur_block_);
#endif

                if (context_verify_result::seq_pending == verify_result &&
                    (rt_reading.version_ == version_)) {
                    rt_reading.traverse_end_block_is_working_ = false;
                } else if (rt_reading.traverse_end_block_is_working_ &&
                           rt_reading.traverse_end_block_ == rt_reading.cur_block_) {
                    if (rt_reading.version_ == version_) {
                        rt_reading.state_ = read_state::traversal_completed;
                    } else {
                        rt_try_traverse_to_next_version();
                    }
                } else if (!rt_reading.traverse_end_block_is_working_ &&
                           !rt_cache_.mem_optimize_.is_block_marked_removed) {
                    rt_reading.traverse_end_block_is_working_ = true;

                    rt_reading.traverse_end_block_ = rt_reading.cur_block_;
                }

                if (context_verify_result::valid == verify_result &&
                    read_state::traversal_completed != rt_reading.state_) {
                    rt_reading.state_ = read_state::hp_block_reading;
                }

                break;
            }

            case read_state::next_group_finding:
                if (!rt_try_traverse_to_next_group()) {
                    rt_reading.state_ = read_state::lp_buffer_reading;

                    if (rt_reading.traverse_end_block_is_working_) {
                        if (rt_reading.traverse_end_block_ == nullptr) {
                            if (rt_reading.version_ == version_) {
                                rt_reading.state_ = read_state::traversal_completed;
                            } else {
                                rt_try_traverse_to_next_version();
                            }
                        }
                    } else {
                        rt_reading.traverse_end_block_is_working_ = true;

                        rt_reading.traverse_end_block_ = nullptr;
                    }
                } else {
                    rt_reading.state_ = read_state::next_block_finding;
                }

                break;

            case read_state::traversal_completed:
                if (rt_reading.cur_block_) {
                    read_handle = rt_reading.cur_block_->get_buffer().read_an_empty_chunk();

                    rt_reading.state_ = read_state::hp_block_reading;
                } else {
                    read_handle = lp_buffer_.read_an_empty_chunk();

                    rt_reading.state_ = read_state::lp_buffer_reading;
                }

                loop_finished = true;
                break;

            default:
                assert(false && "unexpected state!");
                break;
        }
    }

    return read_handle;
}

void log_buffer::return_read_chunk_full_impl(const log_buffer_read_handle& handle) {
    if (rt_cache_.current_reading_.cur_block_) {
        rt_cache_.current_reading_.cur_block_->get_buffer().return_batch_read_chunks(
            rt_cache_.current_reading_.hp_handle_cache_);
    } else if (enum_buffer_result_code::success == handle.result) {
        log_buffer_read_handle handle_cpy = handle;

        handle_cpy.data_addr -= sizeof(context_head);
        handle_cpy.data_size += (std::uint32_t)sizeof(context_head);

        const auto& context = *reinterpret_cast<const context_head*>(handle_cpy.data_addr);

        deregister_seq(context);

        QLOG_UNLIKELY_IF(context.is_external_ref_) {
            rt_return_oversize_read_chunk(handle_cpy);
        }
        else {
            lp_buffer_.return_read_chunk(handle_cpy);
        }
    } else {
        lp_buffer_.return_read_chunk(handle);
    }
}
}  // namespace qlog::buffer