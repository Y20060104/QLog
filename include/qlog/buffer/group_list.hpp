#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>

#include "qlog/buffer/block_list.hpp"
#include "qlog/buffer/log_buffer_config.hpp"
#include "qlog/buffer/log_buffer_defs.hpp"
#include "qlog/buffer/memory_pool.hpp"
#include "qlog/buffer/normal_buffer.hpp"
#include "qlog/platform/macros.hpp"
#include "qlog/platform/memory_map.hpp"
#include "qlog/platform/spin_lock.hpp"
#include "qlog/platform/time_ops.hpp"
#include "qlog/utility/utility_types.hpp"

namespace qlog::buffer {

class group_list;

QLOG_PACK_BEGIN
struct alignas(qlog::utility::QLOG_CACHE_LINE_SIZE) group_data_head {
    block_list used_;
    block_list free_;
    block_list stage_;

    group_data_head(std::uint16_t max_blocks_count, std::uint8_t* group_data_addr,
                    std::size_t group_data_size, bool is_memory_recovery);
} QLOG_PACK_END

    static_assert(sizeof(group_data_head) == sizeof(block_list) * 3,
                  "invalid group_data_head size");

class group_node : public memory_pool_obj_base<group_node, true> {
   public:
    struct pointer_type {
        qlog::platform::spin_lock_rw_crazy lock_;
        group_node* node_ = nullptr;

        qlog_forceinline bool is_empty() const {
            return node_ == nullptr;
        }
    };

   private:
    std::size_t get_group_meta_size(const LogBufferConfig& config);

    std::size_t get_group_data_size(const LogBufferConfig& config,
                                    std::uint16_t max_block_count_per_group);

    create_memory_map_result create_memory_map(const LogBufferConfig& config,
                                               std::uint16_t max_block_count_per_group,
                                               std::uint64_t index);

    bool try_recover_from_memory_map(const LogBufferConfig& config,
                                     std::uint16_t max_block_count_per_group);

    void init_memory_map(const LogBufferConfig& config, std::uint16_t max_block_count_per_group);

    void init_memory(const LogBufferConfig& config, std::uint16_t max_block_count_per_group);

   private:
    pointer_type next_;
    std::unique_ptr<normal_buffer> buffer_entity_;
    group_data_head* head_ptr_ = nullptr;
    std::uint64_t in_pool_epoch_ms_ = 0;
    group_list* parent_list_ = nullptr;

   public:
    group_node(group_list* parent_list, std::uint16_t max_block_count_per_group,
               std::uint64_t index);

    ~group_node();

    qlog_forceinline pointer_type& get_next_ptr() {
        return next_;
    }

    qlog_forceinline group_data_head& get_data_head() {
        QLOG_SUPPRESS_NULL_DEREF_BEGIN()
#if defined(QLOG_DEBUG)
        assert(head_ptr_ && "head_ptr_ is null, group_node has not been initialized");
#endif
        return *head_ptr_;
        QLOG_SUPPRESS_NULL_DEREF_END()
    }  // QLOG_SUPPRESS_NULL_DEREF_BEGIN()是什么？

    qlog_forceinline std::uint64_t get_in_pool_epoch_ms() const {
        return in_pool_epoch_ms_;
    }

    qlog_forceinline void set_in_pool_epoch_ms(std::uint64_t epoch_ms) {
        in_pool_epoch_ms_ = epoch_ms;  // 这个in pool epoch ms和日志记录的epoch ms 有什么不同嘛？
    }

    qlog_forceinline bool is_range_include(const block_node_head* block) const {
        return head_ptr_->used_.is_range_include(block);
    }

#if defined(QLOG_UNIT_TEST)
    qlog_forceinline create_memory_map_result get_memory_map_status() const {
        return buffer_entity_->get_mmap_result();
    }
#endif
};

class group_list {
   public:
    enum class lock_type { no_lock, read_lock, write_lock };

    struct iterator {
        friend class group_list;

       private:
        group_node::pointer_type* last_pointer_;
        group_node* value_;
        lock_type last_lock_type_;

       public:
        iterator() : last_pointer_(nullptr), value_(nullptr), last_lock_type_(lock_type::no_lock) {}

        qlog_forceinline operator bool() const {
            return static_cast<bool>(value_);
        }

        qlog_forceinline group_node& value() {
            return *value_;
        }

        qlog_forceinline const group_node& value() const {
            return *value_;
        }

        qlog_forceinline bool operator==(const iterator& rhs) const {
            return value_ == rhs.value_;
        }

        qlog_forceinline bool operator!=(const iterator& rhs) const {
            return value_ != rhs.value_;
        }
    };

    static constexpr std::uint64_t GROUP_NODE_GC_LIFE_TIME_MS = 1000;

   public:
    group_list(const LogBufferConfig& config, std::uint16_t max_block_count_per_group);

    ~group_list();

    block_node_head* alloc_new_block(const void* misc_data_src, std::size_t misc_data_size);

    void recycle_block_thread_unsafe(iterator group, block_node_head* prev_block,
                                     block_node_head* recycle_block);

    qlog_forceinline std::uint16_t get_max_block_count_per_group() const {
        return max_block_count_per_group_;
    }

#if defined(QLOG_UNIT_TEST)
    qlog_forceinline std::int32_t get_groups_count() const {
        return groups_count_.load_seq_cst();
    }
#endif

    void garbage_collect();

    std::size_t get_garbage_count();

    qlog_forceinline iterator first(const lock_type type) {
        iterator result;
        result.last_pointer_ = &head_;
        result.last_lock_type_ = type;

        switch (type) {
            case lock_type::no_lock:
                break;
            case lock_type::read_lock:
                head_.lock_.read_lock();
                break;
            case lock_type::write_lock:
                head_.lock_.write_lock();
                break;
            default:
                break;
        }

        result.value_ = head_.node_;

        if (!result.value_) {
            switch (type) {
                case lock_type::no_lock:
                    break;
                case lock_type::read_lock:
                    head_.lock_.read_unlock();
                    break;
                case lock_type::write_lock:
                    head_.lock_.write_unlock();
                    break;
                default:
                    break;
            }
        }

        return result;
    }  // 这个first是在干什么？ 读取 head对应的group node嘛？

    qlog_forceinline iterator next(iterator& current, lock_type type) {
        switch (type) {
            case lock_type::no_lock:
                break;
            case lock_type::read_lock:
                current.value_->get_next_ptr().lock_.read_lock();
                break;
            case lock_type::write_lock:
                current.value_->get_next_ptr().lock_.write_lock();
                break;
            default:
                break;
        }

        iterator result;
        result.last_pointer_ = &current.value_->get_next_ptr();
        result.value_ = current.value_->get_next_ptr().node_;
        result.last_lock_type_ = type;

        if (!result.value_) {
            switch (type) {
                case lock_type::no_lock:
                    break;
                case lock_type::read_lock:
                    current.value_->get_next_ptr().lock_.read_unlock();
                    break;
                case lock_type::write_lock:
                    current.value_->get_next_ptr().lock_.write_unlock();
                    break;
                default:
                    break;
            }
        }

        switch (current.last_lock_type_) {
            case lock_type::no_lock:
                break;
            case lock_type::read_lock:
                current.last_pointer_->lock_.read_unlock();
                break;
            case lock_type::write_lock:
                current.last_pointer_->lock_.write_unlock();
                break;
            default:
                break;
        }

        return result;
    }

    qlog_forceinline void delete_and_unlock_thread_unsafe(iterator& current) {
        current.value_->get_next_ptr().lock_.write_lock();
        group_node* next_node = current.value().get_next_ptr().node_;
        current.value_->get_next_ptr().lock_.write_unlock();

        current.last_pointer_->node_ = next_node;

        switch (current.last_lock_type_) {
            case lock_type::no_lock:
                break;
            case lock_type::read_lock:
                current.last_pointer_->lock_.read_unlock();
                break;
            case lock_type::write_lock:
                current.last_pointer_->lock_.write_unlock();
                break;
            default:
                break;
        }

        current.value().set_in_pool_epoch_ms(qlog::platform::high_performance_epoch_ms());

        pool_.push(&current.value());

#if defined(QLOG_UNIT_TEST)
        groups_count_.fetch_add_seq_cst(-1);
#endif
    }

    qlog_forceinline const LogBufferConfig& get_config() const {
        return config_;
    }

   private:
    const LogBufferConfig& config_;
    std::uint16_t max_block_count_per_group_;

#if defined(QLOG_UNIT_TEST)
    qlog::platform::atomic<std::int32_t> groups_count_ = 0;
#endif

    alignas(qlog::utility::QLOG_CACHE_LINE_SIZE)
        qlog::platform::atomic<std::uint64_t> current_group_index_;

    alignas(qlog::utility::QLOG_CACHE_LINE_SIZE) group_node::pointer_type head_;

    alignas(qlog::utility::QLOG_CACHE_LINE_SIZE) memory_pool<group_node> pool_;
};

}  // namespace qlog::buffer