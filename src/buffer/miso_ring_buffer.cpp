#include "qlog/buffer/miso_ring_buffer.hpp"

#include <algorithm>
#include <cstring>
#include <memory>
#include <string>

#include "qlog/global/log_vars.hpp"
#include "qlog/io/file_manager.hpp"
#include "qlog/utility/hash_map.hpp"
#include "qlog/utility/utility.hpp"

namespace qlog::buffer {

struct miso_tls_buffer_info {
    std::uint32_t wt_read_cursor_cache_;
    bool is_new_created = true;
};

typedef qlog::utility::hash_map_inline<const miso_ring_buffer*, miso_tls_buffer_info>
    miso_tls_buffer_map_type;

struct miso_tls_info {
   private:
    miso_tls_buffer_map_type* buffer_map_;
    const miso_ring_buffer* cur_buffer_;
    miso_tls_buffer_info* cur_buffer_info_;

   public:
    miso_tls_info() {
        buffer_map_ = nullptr;
        cur_buffer_ = nullptr;
        cur_buffer_info_ = nullptr;
    }

    miso_tls_buffer_info& get_buffer_info(const miso_ring_buffer* buffer) {
        if (buffer == cur_buffer_) {
            return *cur_buffer_info_;
        }

        if (!buffer_map_) {
            buffer_map_ = new miso_tls_buffer_map_type();
            buffer_map_->set_expand_rate(4);
        }

        cur_buffer_ = buffer;
        cur_buffer_info_ = &(*buffer_map_)[buffer];

        return *cur_buffer_info_;
    }

    ~miso_tls_info() {
        if (buffer_map_) {
            delete buffer_map_;
            buffer_map_ = nullptr;
        }

        cur_buffer_ = nullptr;
        cur_buffer_info_ = nullptr;
    }
};
QLOG_TLS_NON_POD(miso_tls_info, miso_tls_info_)

miso_ring_buffer::miso_ring_buffer(const log_buffer_config& config)
    : config_(config), head_(nullptr), aligned_blocks_(nullptr), aligned_blocks_count_(0) {
    assert((QLOG_POD_RUNTIME_OFFSET_OF(block::chunk_head_def, data) % 8 == 0) &&
           "invalid chunk_head size, it must be a multiple of 8 "
           "to ensure the `data` is 8 bytes aligned");  // 为什么计算出offset 要-0x10000

    const_cast<log_buffer_config&>(config_).default_buffer_size =
        (std::max)((std::uint32_t)(16 * qlog::utility::QLOG_CACHE_LINE_SIZE),
                   qlog::utility::round_pow_of_two(config_.default_buffer_size));

    assert((std::uintptr_t)&cursors_.write_cursor_ %
               (std::uintptr_t)qlog::utility::QLOG_CACHE_LINE_SIZE ==
           0);

    assert((std::uintptr_t)&cursors_.read_cursor_ %
               (std::uintptr_t)qlog::utility::QLOG_CACHE_LINE_SIZE ==
           0);

    auto mmap_result = create_memory_map();

    aligned_blocks_count_ = config_.default_buffer_size >> qlog::utility::QLOG_CACHE_LINE_SIZE_LOG2;

    head_ = static_cast<head*>(buffer_entity_->data());

    aligned_blocks_ = (miso_ring_buffer::block*)(head_ + 1);

    switch (mmap_result) {
        case create_memory_map_result::failed:
            init_with_memory();
            break;

        case create_memory_map_result::use_existed:
            if (!try_recover_from_exist_memory_map()) {
                init_with_memory_map();
            }
            break;

        case create_memory_map_result::new_created:
            init_with_memory_map();
            break;

        default:
            break;
    }

#if defined(QLOG_DEBUG)
    total_write_bytes_ = 0;
    total_read_bytes_ = 0;

    for (std::int32_t i = 0; i < (std::int32_t)enum_buffer_result_code::result_code_count; ++i) {
        result_code_statistics_[i].store_release(0);
    }
#endif
}

miso_ring_buffer::~miso_ring_buffer() {
    head_ = nullptr;
}

create_memory_map_result miso_ring_buffer::create_memory_map() {
    std::string path = TO_ABSOLUTE_PATH(
        "qlog_mmap/mmap_" + config_.log_name + "/lp/" + config_.log_name + ".mmap", 0);

    std::size_t head_size = sizeof(head);

    std::size_t map_size = (std::uint32_t)(config_.default_buffer_size + head_size);

    buffer_entity_ = std::make_unique<normal_buffer>(map_size, config_.need_recovery ? path : "",
                                                     true);  // 传入的三个参数是什么？

    return buffer_entity_->get_mmap_result();
}

void miso_ring_buffer::init_with_memory_map() {
    for (std::uint32_t i = 0; i < aligned_blocks_count_; ++i) {
        BUFFER_ATOMIC_CAST_IGNORE_ALIGNMENT(aligned_blocks_[i].chunk_head.status, block_status)
            .store_release(miso_ring_buffer::block_status::unused);
    }

    head_->read_cursor_cache_ = 0;
    head_->read_cursor_start_cache_ = 0;
    head_->log_checksum_ = config_.calculate_check_sum();

    std::memset(head_->mmap_misc_data_, 0, sizeof(head_->mmap_misc_data_));

    cursors_.write_cursor_.store_release(0);
    cursors_.read_cursor_.store_release(0);

    mmap_buffer_state_ = memory_map_buffer_state::init_with_memory;  // 不是init with mmap吗？
}

bool miso_ring_buffer::try_recover_from_exist_memory_map() {
    if (head_->log_checksum_ != config_.calculate_check_sum()) {
        return false;
    }

    head_->read_cursor_cache_ = head_->read_cursor_cache_ & (aligned_blocks_count_ - 1);

    head_->read_cursor_start_cache_ = head_->read_cursor_cache_;

    std::uint32_t current_cursor = head_->read_cursor_cache_;

    bool data_parse_finished = false;

    while (!data_parse_finished) {
        block& current_block = cursor_to_block(current_cursor);

        auto block_num = current_block.chunk_head.get_block_num();

        auto data_size = current_block.chunk_head.data_size;

        auto status =
            BUFFER_ATOMIC_CAST_IGNORE_ALIGNMENT(current_block.chunk_head.status, block_status)
                .load_raw();

        switch (status) {
            case block_status::used:
                if ((current_cursor & (aligned_blocks_count_ - 1)) + block_num >
                    aligned_blocks_count_) {
                    return false;
                }
                if ((std::size_t)data_size >
                        static_cast<std::size_t>(
                            static_cast<std::intptr_t>(block_num * sizeof(block)) -
                            data_block_offset) ||
                    (std::size_t)data_size <=
                        (block_num == 1
                             ? 0
                             : static_cast<std::size_t>(
                                   static_cast<std::intptr_t>(block_num * sizeof(block)) -
                                   static_cast<std::ptrdiff_t>(sizeof(block)) -
                                   data_block_offset))) {
                    return false;
                }  // 这是判断数据大小比剩余总大小要大 或者如果只有一块 就返回 如果大于一块
                   // 就判断是不是小于总大小-block 以及第二块的 数据偏移？

                current_cursor += block_num;
                break;

            case block_status::invalid:
                current_cursor += block_num;

                if ((current_cursor & (aligned_blocks_count_ - 1)) + block_num >
                    aligned_blocks_count_) {
                    return false;
                }
                break;

            case block_status::unused:
                data_parse_finished = true;
                break;

            default:
                return false;
                break;
        }

        std::uint32_t total_size_parsed =
            static_cast<std::uint32_t>(current_cursor - head_->read_cursor_cache_);

        if (total_size_parsed == aligned_blocks_count_) {
            data_parse_finished = true;
        } else if (total_size_parsed > aligned_blocks_count_) {
            return false;
        }
    }

    if (current_cursor - head_->read_cursor_cache_ > aligned_blocks_count_) {
        return false;
    }

    cursors_.write_cursor_.store_release(current_cursor);

    cursors_.read_cursor_.store_release(head_->read_cursor_cache_);

    for (std::uint32_t i = current_cursor; i < head_->read_cursor_cache_ + aligned_blocks_count_;
         ++i) {  // 为什么read cache 还要+整个aligned count？
        BUFFER_ATOMIC_CAST_IGNORE_ALIGNMENT(cursor_to_block(i).chunk_head.status, block_status)
            .store_release(block_status::unused);
    }

    mmap_buffer_state_ = memory_map_buffer_state::recover_from_memory_map;

    return true;
}

void miso_ring_buffer::init_with_memory() {
    for (std::uint32_t i = 0; i < aligned_blocks_count_; ++i) {
        BUFFER_ATOMIC_CAST_IGNORE_ALIGNMENT(aligned_blocks_[i].chunk_head.status, block_status)
            .store(miso_ring_buffer::block_status::unused, qlog::platform::memory_order::release);
    }
    head_->read_cursor_cache_ = 0;
    head_->read_cursor_start_cache_ = 0;
    head_->log_checksum_ = 0;

    std::memset(head_->mmap_misc_data_, 0, sizeof(head_->mmap_misc_data_));

    cursors_.write_cursor_.store_release(0);
    cursors_.read_cursor_.store_release(0);

    mmap_buffer_state_ = memory_map_buffer_state::init_with_memory;
}
void miso_ring_buffer::set_thread_check_enable(bool in_enable) {
#if defined(QLOG_DEBUG)
    check_thread_ = in_enable;

    if (!check_thread_) {
        read_thread_id_ = empty_thread_id_;
    }
#else
    (void)in_enable;
#endif
}

bool miso_ring_buffer::is_thread_check_enable() const {
#if defined(QLOG_DEBUG)
    return check_thread_ && qlog::log_global_vars::get().is_thread_check_enabled();
#else
    return false;
#endif
}

log_buffer_write_handle miso_ring_buffer::alloc_write_chunk(std::uint32_t size) {
    log_buffer_write_handle handle;

    std::uint32_t size_required = size + (std::uint32_t)data_block_offset;

    std::uint32_t need_block_count_tmp =
        static_cast<std::uint32_t>((size_required + (qlog::utility::QLOG_CACHE_LINE_SIZE - 1)) >>
                                   qlog::utility::QLOG_CACHE_LINE_SIZE_LOG2);

    if (need_block_count_tmp > aligned_blocks_count_ || need_block_count_tmp == 0 ||
        need_block_count_tmp > block::MAX_BLOCK_NUM_PER_CHUNK) {
#if defined(QLOG_DEBUG)
        ++result_code_statistics_[(std::uint32_t)enum_buffer_result_code::err_alloc_size_invalid];
#endif
        handle.result = enum_buffer_result_code::err_alloc_size_invalid;
        return handle;
    }

    std::uint32_t need_block_count = need_block_count_tmp;

    std::uint32_t current_write_cursor = cursors_.write_cursor_.load_relaxed();

    std::uint32_t read_cursor_tmp_when_tls_recycled;
    std::uint32_t* read_cursor_ptr = nullptr;

    if (miso_tls_info_) {  // miso tls info 是什么？
        auto& tls_info = miso_tls_info_.get().get_buffer_info(this);

        if (tls_info.is_new_created) {
            tls_info.is_new_created = false;
            tls_info.wt_read_cursor_cache_ = cursors_.read_cursor_.load_acquire();
        }

        read_cursor_ptr = &tls_info.wt_read_cursor_cache_;
    } else {
        read_cursor_ptr = &read_cursor_tmp_when_tls_recycled;

        read_cursor_tmp_when_tls_recycled = cursors_.read_cursor_.load_acquire();
    }

    std::uint32_t& read_cursor_ref = *read_cursor_ptr;

    std::uint32_t used_blocks_count =
        static_cast<std::uint32_t>(current_write_cursor - read_cursor_ref);

    while (true) {
        std::uint32_t new_cursor = current_write_cursor + need_block_count;

        if (static_cast<std::uint32_t>(new_cursor - read_cursor_ref) > aligned_blocks_count_) {
            read_cursor_ref = cursors_.read_cursor_.load_acquire();

            if (static_cast<std::uint32_t>(new_cursor - read_cursor_ref) > aligned_blocks_count_) {
                QLOG_UNLIKELY_IF((need_block_count << 1) > aligned_blocks_count_) {
#if defined(QLOG_DEBUG)
                    ++result_code_statistics_[(std::uint32_t)enum_buffer_result_code::err_alloc_size_invalid];
#endif

                    handle.result = enum_buffer_result_code::err_alloc_size_invalid;
                    return handle;
                }

#if defined(QLOG_DEBUG)
                ++result_code_statistics_[(std::int32_t)enum_buffer_result_code::err_not_enough_space];
#endif

                handle.result = enum_buffer_result_code::err_not_enough_space;
                return handle;
            }
        }

        {
            current_write_cursor = cursors_.write_cursor_.fetch_add_relaxed(need_block_count);

            std::uint32_t next_write_cursor = current_write_cursor + need_block_count;

            if (static_cast<std::uint32_t>(next_write_cursor - read_cursor_ref) >=
                aligned_blocks_count_) {
                while (static_cast<std::uint32_t>(
                           next_write_cursor -
                           (read_cursor_ref = cursors_.read_cursor_.load_acquire())) >=
                       aligned_blocks_count_) {
                    std::uint32_t expected = next_write_cursor;

                    if (cursors_.write_cursor_.compare_exchange_strong(
                            expected, current_write_cursor, qlog::platform::memory_order::relaxed,
                            qlog::platform::memory_order::relaxed)) {  // 这是exchange
                                                                       // 失败进入还是成功进入？
                        QLOG_UNLIKELY_IF((need_block_count << 1) > aligned_blocks_count_) {
#if defined(QLOG_DEBUG)
                            ++result_code_statistics_[(
                                std::uint32_t)enum_buffer_result_code::err_alloc_size_invalid];
#endif

                            handle.result = enum_buffer_result_code::err_alloc_size_invalid;
                            return handle;
                        }

#if defined(QLOG_DEBUG)
                        ++result_code_statistics_[(std::int32_t)enum_buffer_result_code::err_not_enough_space];
#endif

                        handle.result = enum_buffer_result_code::err_not_enough_space;
                        return handle;
                    }
                }
            }
        }
        block& new_block = cursor_to_block(current_write_cursor);

        new_block.chunk_head.set_block_num(need_block_count);

        if ((current_write_cursor & (aligned_blocks_count_ - 1)) + need_block_count >
            aligned_blocks_count_) {
            handle.result = enum_buffer_result_code::err_data_not_contiguous;

#if defined(QLOG_DEBUG)
            ++result_code_statistics_[(std::int32_t)enum_buffer_result_code::err_data_not_contiguous];
#endif

            BUFFER_ATOMIC_CAST_IGNORE_ALIGNMENT(new_block.chunk_head.status, block_status)
                .store_release(qlog::buffer::miso_ring_buffer::block_status::invalid);

            continue;
        }

        new_block.chunk_head.data_size = size;

        handle.result = enum_buffer_result_code::success;
        handle.data_addr = new_block.chunk_head.data;

        used_blocks_count += need_block_count;

        handle.low_space_flag = ((used_blocks_count << 1) >= aligned_blocks_count_);

        if (handle.low_space_flag) {
            read_cursor_ref = cursors_.read_cursor_.load_acquire();

            used_blocks_count =
                static_cast<std::uint32_t>(current_write_cursor + need_block_count -
                                           read_cursor_ref);  // 要不要先-防止溢出再+？

            handle.low_space_flag = ((used_blocks_count << 1) >= aligned_blocks_count_);
        }

#if defined(QLOG_DEBUG)
        ++result_code_statistics_[(std::int32_t)enum_buffer_result_code::success];
#endif

        return handle;
    }
    return handle;
}

/*参考认为 TLS 缓存已回收时，改用本次调用的栈变量：
read_cursor_ptr = &read_cursor_tmp_when_tls_recycled;
两条路径最终统一到：
std::uint32_t& read_cursor_ref = *read_cursor_ptr;
后面的算法只更新这个引用，不必重复两套容量判断。

问：后面算法更新这个引用是什么意思？这个不应该只读吗？后续直接更新的cursors中的aotmic
*/
/*> 和 >= 必须分别保留
阶段	参考条件
预留前容量检查	new_cursor - read_cursor_ref > N
预留后检查	next_write_cursor - read_cursor_ref >= N
回退等待循环	>= N

问：为什么 不能都>=?*/

void miso_ring_buffer::commit_write_chunk(const log_buffer_write_handle& handle) {
    if (handle.result != enum_buffer_result_code::success) {
        return;
    }

    block* block_ptr = (block*)(handle.data_addr - data_block_offset);

    BUFFER_ATOMIC_CAST_IGNORE_ALIGNMENT(block_ptr->chunk_head.status, block_status)
        .store_release(qlog::buffer::miso_ring_buffer ::block_status::used);

#if defined(QLOG_DEBUG)
    total_write_bytes_ += block_ptr->chunk_head.get_block_num() * sizeof(block);
#endif
}

log_buffer_read_handle miso_ring_buffer::read_chunk() {
#if defined(QLOG_DEBUG)
    assert(!is_read_chunk_waiting_for_return_ &&
           "You cant read a chunk before you returning last chunk back");

    if (is_thread_check_enable()) {
        if (read_thread_id_ == 0) {
            read_thread_id_ = qlog::platform::get_current_thread_id();
        }
        qlog::platform::thread_id current_thread_id = qlog::platform::get_current_thread_id();

        assert(current_thread_id == read_thread_id_ &&
               "only single thread reading is supported for miso_ring_buffer!");
    }

    is_read_chunk_waiting_for_return_ = true;
#endif

    log_buffer_read_handle handle;

    while (true) {
        block& block_ref = cursor_to_block(head_->read_cursor_cache_);

        auto status = BUFFER_ATOMIC_CAST_IGNORE_ALIGNMENT(block_ref.chunk_head.status, block_status)
                          .load_acquire();

        auto block_count = block_ref.chunk_head.get_block_num();
        switch (status) {
            case block_status::invalid:
#if defined(QLOG_DEBUG)
                assert((head_->read_cursor_cache_ & (aligned_blocks_count_ - 1)) + block_count >
                       aligned_blocks_count_);

                assert(block_count <= aligned_blocks_count_);
#endif

                block_ref.chunk_head.status = block_status::unused;
                head_->read_cursor_cache_ += block_count;

                continue;
                break;

            case block_status::unused:
                handle.result = enum_buffer_result_code::err_empty_log_buffer;
                break;

            case block_status::used:
#if defined(QLOG_DEBUG)
                assert((head_->read_cursor_cache_ & (aligned_blocks_count_ - 1)) + block_count <=
                       aligned_blocks_count_);
#endif

                handle.result = enum_buffer_result_code::success;
                handle.data_addr = block_ref.chunk_head.data;
                handle.data_size = block_ref.chunk_head.data_size;
                break;

            default:
                assert(false && "invalid read ring buffer block status");
                break;
        }
        break;
    }

#if defined(QLOG_DEBUG)
    ++result_code_statistics_[(std::int32_t)handle.result];
#endif

    return handle;
}

log_buffer_read_handle miso_ring_buffer::read_an_empty_chunk() {
#if defined(QLOG_DEBUG)
    assert(!is_read_chunk_waiting_for_return_ &&
           "You cant read a chunk before you returning last chunk back");

    if (is_thread_check_enable()) {
        qlog::platform::thread_id current_thread_id = qlog::platform::get_current_thread_id();

        assert((current_thread_id == read_thread_id_ || 0 == read_thread_id_) &&
               "only single thread reading is supported for miso_ring_buffer!");
    }

    is_read_chunk_waiting_for_return_ = true;
#endif

    log_buffer_read_handle handle;
    handle.result = enum_buffer_result_code::err_empty_log_buffer;

    return handle;
}

void miso_ring_buffer::discard_read_chunk(log_buffer_read_handle& handle) {
#if defined(QLOG_DEBUG)
    assert(is_read_chunk_waiting_for_return_ && "You cant discard a chunk has not been read yet");

    is_read_chunk_waiting_for_return_ = false;

    if (is_thread_check_enable()) {
        qlog::platform::thread_id current_thread_id = qlog::platform::get_current_thread_id();

        assert((current_thread_id == read_thread_id_ || 0 == read_thread_id_) &&
               "only single thread reading is supported for miso_ring_buffer!");
    }
#endif

    handle.result = enum_buffer_result_code::err_empty_log_buffer;
}

void miso_ring_buffer::return_read_chunk(const log_buffer_read_handle& handle) {
#if defined(QLOG_DEBUG)
    assert(is_read_chunk_waiting_for_return_ && "You cant return a chunk has not been read yet");

    is_read_chunk_waiting_for_return_ = false;
#endif

#if defined(QLOG_DEBUG)
    if (is_thread_check_enable()) {
        qlog::platform::thread_id current_thread_id = qlog::platform::get_current_thread_id();

        assert((current_thread_id == read_thread_id_ || 0 == read_thread_id_) &&
               "only single thread reading is supported for miso_ring_buffer!");
    }

    if (handle.result == enum_buffer_result_code::success) {
        assert((handle.data_addr <= (std::uint8_t*)(aligned_blocks_ + aligned_blocks_count_)) &&
               (handle.data_addr >= (std::uint8_t*)aligned_blocks_) &&
               "please don't return a read handle not belongs to this miso_ring_buffer");
    }
#endif

    bool need_flush = false;
    std::uint32_t start_cursor = head_->read_cursor_start_cache_;

    if (handle.result == enum_buffer_result_code::err_empty_log_buffer) {
        need_flush = true;
    } else if (handle.result == enum_buffer_result_code::success) {
        head_->read_cursor_cache_ +=
            cursor_to_block(head_->read_cursor_cache_).chunk_head.get_block_num();

        need_flush = (head_->read_cursor_cache_ - start_cursor >= (aligned_blocks_count_ >> 2));
    }

    if (!need_flush) {
        return;
    }

    std::uint32_t block_count = head_->read_cursor_cache_ - start_cursor;

    if (block_count > 0) {
        for (std::uint32_t i = 0; i < block_count; ++i) {
            cursor_to_block(start_cursor + i).chunk_head.status = block_status::unused;
        }

#if defined(QLOG_DEBUG)
        total_read_bytes_ += block_count * sizeof(block);
#endif

        head_->read_cursor_start_cache_ = head_->read_cursor_cache_;

        cursors_.read_cursor_.store_release(head_->read_cursor_cache_);
    }
}

void miso_ring_buffer::data_traverse(void (*in_callback)(std::uint8_t* data, std::uint32_t size,
                                                         void* user_data),
                                     void* in_user_data) {
#if defined(QLOG_DEBUG)
    if (is_thread_check_enable()) {
        if (0 == read_thread_id_) {
            read_thread_id_ = qlog::platform::get_current_thread_id();
        }

        qlog::platform::thread_id current_thread_id = qlog::platform::get_current_thread_id();

        assert(current_thread_id == read_thread_id_ &&
               "only single thread reading is supported for miso_ring_buffer!");
    }
#endif

    std::uint32_t current_read_cursor = cursors_.read_cursor_.load_acquire();

    bool finished = false;

    while (!finished) {
        block& block_ref = cursor_to_block(current_read_cursor);

        auto status = BUFFER_ATOMIC_CAST_IGNORE_ALIGNMENT(block_ref.chunk_head.status, block_status)
                          .load_acquire();

        auto block_count = block_ref.chunk_head.get_block_num();

        switch (status) {
            case block_status::invalid:
#if defined(QLOG_DEBUG)
                assert((current_read_cursor & (aligned_blocks_count_ - 1)) + block_count >
                       aligned_blocks_count_);

                assert(block_count <= aligned_blocks_count_);
#endif
                break;

            case block_status::unused:
                finished = true;
                break;

            case block_status::used:
#if defined(QLOG_DEBUG)
                assert((current_read_cursor & (aligned_blocks_count_ - 1)) + block_count <=
                       aligned_blocks_count_);
#endif

                in_callback(block_ref.chunk_head.data, block_ref.chunk_head.data_size,
                            in_user_data);
                break;

            default:
                assert(false && "invalid read ring buffer block status");
                break;
        }

        current_read_cursor = current_read_cursor + block_count;

        if (current_read_cursor - cursors_.read_cursor_.load_raw() >= aligned_blocks_count_) {
            finished = true;
        }
    }
}
// data_traverse有什么用？
/*例如 N = 16、本次需要 16 块、读写游标均为 0：预检的 16 > 16 不成立，但预留后的 16 >= 16
 * 成立，随后会尝试回退。因此，预检通过不代表最终分配必然成功。检验在预留之前吧？检验不成立
 * 不会进入预留啊*/
}  // namespace qlog::buffer
