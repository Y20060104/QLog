#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "qlog/buffer/group_list.hpp"
#include "qlog/buffer/log_buffer_config.hpp"
#include "qlog/buffer/log_buffer_defs.hpp"
#include "qlog/buffer/miso_ring_buffer.hpp"
#include "qlog/buffer/oversize_buffer.hpp"
#include "qlog/buffer/siso_ring_buffer.hpp"
#include "qlog/platform/atomic/atomic.hpp"
#include "qlog/platform/macros.hpp"
#include "qlog/platform/spin_lock.hpp"
#include "qlog/platform/thread_ops.hpp"
#include "qlog/utility/diagnostics.hpp"
#include "qlog/utility/hash_map.hpp"
#include "qlog/utility/type_tools.hpp"
#include "qlog/utility/utility_types.hpp"

namespace qlog::buffer {

class alignas(qlog::utility::QLOG_CACHE_LINE_SIZE) log_buffer {
   public:
#if defined(QLOG_MOBILE_PLATFORM)
    static constexpr std::uint16_t BLOCKS_PER_GROUP_NODE = 2;
#else
    static constexpr std::uint16_t BLOCKS_PER_GROUP_NODE = 16;
#endif

#if defined(QLOG_UNIT_TEST)
    static constexpr std::uint16_t MAX_RECOVERY_VERSION_RANGE = 5;
#else
    static constexpr std::uint16_t MAX_RECOVERY_VERSION_RANGE = 2;
#endif

    static constexpr std::uint64_t HP_BUFFER_CALL_FREQUENCY_CHECK_INTERVAL = 1000;

    static constexpr std::uint64_t OVERSIZE_BUFFER_RECYCLE_INTERVAL_MS = 1000;

   public:
    QLOG_PACK_BEGIN
    struct lp_buffer_head_misc {
        std::uint16_t saved_version_;
    } QLOG_PACK_END

        struct destruction_mark {
        qlog::platform::spin_lock lock_;
        bool is_destructed_ = false;
    };

    struct oversize_buffer_obj_def {
        oversize_buffer buffer_;
        qlog::platform::spin_lock_rw_crazy buffer_lock_;
        std::uint64_t last_used_epoch_ms_;
        bool is_thread_finished_;

        oversize_buffer_obj_def(std::uint32_t size, const std::string& mmap_file_abs_path,
                                bool auto_create)
            : buffer_(size, mmap_file_abs_path, auto_create),
              last_used_epoch_ms_(0),
              is_thread_finished_(false) {}
    };

    struct alignas(qlog::utility::QLOG_CACHE_LINE_SIZE) log_tls_buffer_info {
        std::uint64_t last_update_epoch_ms_ = 0;
        std::uint64_t update_times_ = 0;
        block_node_head* cur_block_ = nullptr;
        log_buffer* buffer_ = nullptr;

        log_buffer_write_handle oversize_parent_handle_;
        oversize_buffer_obj_def* oversize_target_buffer_;

        qlog::utility::shared_ptr<destruction_mark> destruction_mark_;

        alignas(qlog::utility::QLOG_CACHE_LINE_SIZE) struct {
            std::uint32_t current_write_seq_ = 0;
        } wt_data_;

        alignas(qlog::utility::QLOG_CACHE_LINE_SIZE) struct {
            std::uint32_t current_read_seq_ = 0;
        } rt_data_;

        ~log_tls_buffer_info();
    };

    static_assert(sizeof(log_tls_buffer_info) % qlog::utility::QLOG_CACHE_LINE_SIZE == 0,
                  "log_tls_buffer_info current_read_seq_ must be 64 bytes aligned");

    struct log_tls_info {
#if !defined(QLOG_DEBUG)
       private:
#endif
        qlog::utility::hash_map_inline<std::uint64_t, log_tls_buffer_info*>* log_map_ = nullptr;

        std::uint64_t cur_log_buffer_id_ = 0;
        log_tls_buffer_info* cur_buffer_info_ = nullptr;

       public:
        qlog_forceinline log_tls_buffer_info& get_buffer_info(const log_buffer* buffer);

        qlog_forceinline log_tls_buffer_info& get_buffer_info_directly(const log_buffer* buffer);

        ~log_tls_info();
    };

    QLOG_PACK_BEGIN
    struct alignas(8) pointer_8_bytes_for_32_bits_system {
        log_tls_buffer_info* ptr;
        std::uintptr_t dummy;
    } QLOG_PACK_END

        QLOG_PACK_BEGIN struct alignas(8) pointer_8_bytes_for_64_bits_system {
        log_tls_buffer_info* ptr;
    } QLOG_PACK_END

        QLOG_PACK_BEGIN struct alignas(8) context_head {
       public:
        std::uint16_t version_;
        bool is_thread_finished_;
        bool is_external_ref_;
        std::uint32_t seq_;

        qlog::utility::condition_type_t<sizeof(void*) == 8, pointer_8_bytes_for_64_bits_system,
                                        pointer_8_bytes_for_32_bits_system>
            tls_info_;

        qlog_forceinline log_tls_buffer_info* get_tls_info() const {
            return tls_info_.ptr;
        }

        qlog_forceinline void set_tls_info(log_tls_buffer_info* tls_info) {
            tls_info_.ptr = tls_info;
        }
    } QLOG_PACK_END

        static_assert(sizeof(context_head) == 16, "context_head size must be 16");

    static_assert(sizeof(context_head) % 8 == 0, "context_head size must be a multiple of 8");

    QLOG_PACK_BEGIN
    struct alignas(8) block_misc_data {
       private:
        alignas(8) char is_removed_place_holder_[sizeof(
            qlog::platform::atomic_trivially_constructible<bool>)];

       public:
        alignas(8) bool need_reallocate_;
        alignas(8) context_head context_;

        qlog_forceinline qlog::platform::atomic_trivially_constructible<bool>& is_removed() {
            return *qlog::utility::launder(
                reinterpret_cast<qlog::platform::atomic_trivially_constructible<bool>*>(
                    is_removed_place_holder_));
        }
    } QLOG_PACK_END

        static_assert(sizeof(block_misc_data) == 16 + sizeof(context_head),
                      "invalid block_misc_data size");

   public:
    log_buffer(log_buffer_config& config);
    ~log_buffer();

    log_buffer_write_handle alloc_write_chunk(std::uint32_t size, std::uint64_t current_epoch_ms);

    void commit_write_chunk(const log_buffer_write_handle& handle);

    qlog_forceinline log_buffer_read_handle read_chunk();

    qlog_forceinline void return_read_chunk(const log_buffer_read_handle& handle);

    qlog_forceinline std::uint16_t get_version() const {
        return version_;
    }

    qlog_forceinline std::uint16_t get_current_reading_version() const {
        return rt_cache_.current_reading_.version_;
    }

    qlog_forceinline bool is_current_reading_recovered() const {
        return rt_cache_.current_reading_.is_in_recovery_reading_;
    }

    qlog_forceinline const log_buffer_config& get_config() const {
        return config_;
    }

#if defined(QLOG_UNIT_TEST)
    const log_tls_buffer_info& get_buffer_info_for_this_thread() const;

    std::int32_t get_groups_count() const {
        return hp_buffer_.get_groups_count();
    }

    void garbage_collect() {
        hp_buffer_.garbage_collect();
    }

    std::size_t get_garbage_count() {
        return hp_buffer_.get_garbage_count();
    }
#endif

   private:
    block_node_head* alloc_new_hp_block();

    enum class context_verify_result {
        valid,
        version_pending,
        version_invalid,
        seq_pending,
        seq_invalid,
    };

    enum class read_state {
        lp_buffer_reading,
        hp_block_reading,
        next_block_finding,
        next_group_finding,
        traversal_completed,
    };

    qlog_forceinline bool is_version_valid(std::uint16_t version) {
        return static_cast<std::uint16_t>(version_ - version) <=
               static_cast<std::uint16_t>(version_ - rt_cache_.current_reading_.version_);
    }

    context_verify_result verify_context(const context_head& context);

    context_verify_result verify_oversize_context(const context_head& parent_context,
                                                  const context_head& oversize_context);

    void deregister_seq(const context_head& context);
    void prepare_and_fix_recovery_data();
    void clear_recovery_data();

    bool rt_read_from_lp_buffer(log_buffer_read_handle& out_handle);

    log_buffer_read_handle read_chunk_full_impl();

    void return_read_chunk_full_impl(const log_buffer_read_handle& handle);

#if defined(QLOG_DEBUG)
    void debug_check_read_thread();
#endif

    bool rt_try_traverse_to_next_block_in_group(context_verify_result& out_verify_result);

    bool rt_try_traverse_to_next_group();
    void rt_try_traverse_to_next_version();
    void refresh_traverse_end_mark();

    log_buffer_write_handle wt_alloc_oversize_write_chunk(std::uint32_t size,
                                                       std::uint64_t current_epoch_ms);

    void wt_commit_oversize_write_chunk(const log_buffer_write_handle& oversize_handle);

    bool rt_read_oversize_chunk(const log_buffer_read_handle& parent_handle,
                                log_buffer_read_handle& out_oversize_handle);

    void rt_return_oversize_read_chunk(const log_buffer_read_handle& oversize_handle);

    void rt_recycle_oversize_buffers();

   private:
    friend struct log_tls_info;

    log_buffer_config config_;
    std::uint64_t id_;
    group_list hp_buffer_;
    miso_ring_buffer lp_buffer_;
    std::uint32_t hp_buffer_max_alloc_size_;
    const std::uint16_t version_ = 0;

    qlog::utility::shared_ptr<destruction_mark> destruction_mark_;

    struct alignas(qlog::utility::QLOG_CACHE_LINE_SIZE) {
        qlog::platform::spin_lock_rw_crazy array_lock_;
        std::vector<std::unique_ptr<oversize_buffer_obj_def>> buffers_array_;
    } temprorary_oversize_buffer_;

    qlog::platform::atomic<std::uint64_t> current_oversize_buffer_index_;

    struct alignas(qlog::utility::QLOG_CACHE_LINE_SIZE) {
        struct {
            group_list::iterator last_group_;
            group_list::iterator cur_group_;
            block_node_head* last_block_ = nullptr;
            block_node_head* cur_block_ = nullptr;

            std::uint16_t version_ = 0;
            bool is_in_recovery_reading_ = true;

            std::vector<qlog::utility::hash_map<void*, std::uint32_t>> recovery_records_;

#ifdef QLOG_UNIT_TEST
            std::vector<qlog::utility::hash_map<
                void*, qlog::utility::hash_map<std::uint32_t, std::uint16_t>>>
                recovery_seq_records_;
#endif

            read_state state_ = read_state::lp_buffer_reading;
            bool traverse_end_block_is_working_ = false;
            block_node_head* traverse_end_block_ = nullptr;

            siso_ring_buffer::siso_buffer_batch_read_handle hp_handle_cache_;
            log_buffer_read_handle rt_oversize_parent_handle_;
            oversize_buffer_obj_def* rt_oversize_target_buffer_ = nullptr;
        } current_reading_;

        struct {
            std::uint32_t left_holes_num_ = 0;
            std::uint16_t cur_group_using_blocks_num_ = 0;
            bool is_block_marked_removed = false;
            context_verify_result verify_result;
        } mem_optimize_;
    } rt_cache_;

#if defined(QLOG_DEBUG)
    alignas(qlog::utility::QLOG_CACHE_LINE_SIZE) qlog::platform::thread_id empty_thread_id_ = 0;

    qlog::platform::thread_id read_thread_id_ = 0;
#endif
};

qlog_forceinline log_buffer::log_tls_buffer_info& log_buffer::log_tls_info::get_buffer_info(
    const log_buffer* buffer) {
    if (buffer->id_ == cur_log_buffer_id_) {
        return *cur_buffer_info_;
    }

    if (!log_map_) {
        log_map_ = new qlog::utility::hash_map_inline<std::uint64_t, log_tls_buffer_info*>();

        log_map_->set_expand_rate(4);
    }

    cur_log_buffer_id_ = buffer->id_;

    auto iter = log_map_->find(buffer->id_);

    if (iter == log_map_->end()) {
        iter = log_map_->add(buffer->id_, qlog::utility::util::aligned_new<log_tls_buffer_info>(
                                              alignof(log_tls_buffer_info)));

        iter->value()->destruction_mark_ = buffer->destruction_mark_;
        iter->value()->buffer_ = const_cast<log_buffer*>(buffer);
    }

    cur_buffer_info_ = iter->value();
    return *cur_buffer_info_;
}

qlog_forceinline log_buffer::log_tls_buffer_info&
log_buffer::log_tls_info::get_buffer_info_directly(const log_buffer* buffer) {
#if defined(QLOG_DEBUG)
    assert(buffer->id_ == cur_log_buffer_id_ &&
           "log_buffer::alloc and log_buffer::commit must use in pair");
#endif

    (void)buffer;
    return *cur_buffer_info_;
}
// oversize 的parent handle是什么？
// current_write_seq_是什么？
//~log_tls_info();  ~log_tls_buffer_info();有什么不同？
//  pointer_8_bytes_for_64_bits_system； pointer_8_bytes_for_32_bits_system是啥？
/*qlog_forceinline std::uint16_t get_current_reading_version() const {
        return rt_cache_.current_reading_.version_;
    }
        这个version是防止ABA嘛？version有什么用*/
/*qlog_forceinline bool is_current_reading_recovered() const {
return rt_cache_.current_reading_.is_in_recovery_reading_;
}
这个recovery是啥？为什么会recovered 不commit为什么会被recovered*/
//// saved_version_存的是什么abamark嘛？这个// saved_version_什么时候会变化？
// log_tls_buffer_info中update_times_记录的是什么？什么时候更新？
// log_map_记录的是什么？
/*context_verify_result verify_oversize_context(
const context_head& parent_context,
const context_head& oversize_context);
这个是什么呢？parent context为什么需要改为oversize context？*/
// logbuffer是管理spsc mpsc oversizebuffer三者都有嘛？oversizebuffer是什么情况？
// pair.value()是什么呢？为什么需要他？
// TLS必须要分配oversize对象嘛？
// 当前在 LP 时，循环申请一个只包含 context_head 的结束记录什么意思？
/*先解锁，再释放 destruction_protector，因此不会在持锁期间因最后一个成员引用释放而销毁锁对象
为什么说 不会在持锁期间销毁锁对象，就算不上锁最后一个成员引用释放 还是会被销毁吧？，而且如果不上锁
销毁有成员引用对应的对象时会不会导致多线程错误销毁呢？*/
// cur_block_非空指的是block list中的block嘛？
// 为什么要用scoped_handle，这里传入的finishmarkhandle就是writehandle，为什么还需要包含readhandle/writehandle的scopredhandle？

qlog_forceinline log_buffer_read_handle log_buffer::read_chunk() {
#if defined(QLOG_DEBUG)
    debug_check_read_thread();
#endif

    auto& hp_handle = rt_cache_.current_reading_.hp_handle_cache_;

    QLOG_LIKELY_IF(hp_handle.result == enum_buffer_result_code::success) {
        if (hp_handle.has_next()) {
            return hp_handle.next();
        }

        hp_handle.result = enum_buffer_result_code::err_empty_log_buffer;
    }

    return read_chunk_full_impl();
}

qlog_forceinline void log_buffer::return_read_chunk(const log_buffer_read_handle& handle) {
#if defined(QLOG_DEBUG)
    debug_check_read_thread();
#endif

    auto& rt_reading = rt_cache_.current_reading_;

    QLOG_LIKELY_IF(rt_reading.hp_handle_cache_.result == enum_buffer_result_code::success) {
#if defined(QLOG_DEBUG)
        assert(rt_reading.hp_handle_cache_.verify_chunk(handle) &&
               "log_buffer::return_read_chunk hp chunk return verify failed");
#endif

        if (!rt_reading.hp_handle_cache_.has_next()) {
#if defined(QLOG_DEBUG)
            assert(rt_reading.cur_block_);
#endif

            rt_reading.cur_block_->get_buffer().return_batch_read_chunks(
                rt_reading.hp_handle_cache_);
        }

        return;
    }

    return_read_chunk_full_impl(handle);
}
}  // namespace qlog::buffer