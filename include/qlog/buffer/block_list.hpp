#pragma once

#include <cassert>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>

#include "qlog/buffer/log_buffer_defs.hpp"
#include "qlog/buffer/spsc_ring_buffer.hpp"
#include "qlog/platform/macros.hpp"
#include "qlog/utility/diagnostics.hpp"
#include "qlog/utility/utility_types.hpp"

namespace qlog::buffer {

QLOG_SUPPRESS_MAYBE_UNINITIALIZED_BEGIN()

QLOG_PACK_BEGIN
class alignas(8) block_node_head {
   public:
    friend class block_list;

    QLOG_PACK_BEGIN
    struct pointer_type {
       private:
        char value[4];

       public:
        qlog_forceinline std::uint32_t& union_value() {
            return *reinterpret_cast<std::uint32_t*>(value);
        }

        qlog_forceinline const std::uint32_t& union_value() const {
            return *reinterpret_cast<const std::uint32_t*>(value);
        }

        qlog_forceinline std::uint16_t& index() {
            return *reinterpret_cast<std::uint16_t*>(value);
        }

        qlog_forceinline const std::uint16_t& index() const {
            return *reinterpret_cast<const std::uint16_t*>(value);
        }

        qlog_forceinline std::uint16_t& aba_mark() {
            return *reinterpret_cast<std::uint16_t*>(value + 2);
        }

        qlog_forceinline const std::uint16_t& aba_mark() const {
            return *reinterpret_cast<const std::uint16_t*>(value + 2);
        }

        qlog_forceinline static bool is_empty_index(std::uint16_t index) {
            return index == static_cast<std::uint16_t>(-1);
        }

        qlog_forceinline bool is_empty() const {
            return is_empty_index(index());
        }
    } QLOG_PACK_END

        static_assert(sizeof(pointer_type) == sizeof(std::uint32_t),
                      "invalid size of pointer_type");

   private:
    pointer_type next_;
    block_list_type type_;
    char padding_[8 - sizeof(next_) - sizeof(type_)];

    alignas(8) char misc_data_[qlog::utility::QLOG_CACHE_LINE_SIZE - 8];
    char buffer_[sizeof(SpscRingBuffer)];

   public:
    block_node_head(block_list_type type, void* buffer, std::size_t buffer_size,
                    bool is_memory_recovery);

    ~block_node_head();

    static void alignment_assert();

    template <typename T>
    qlog_forceinline T& get_misc_data() {
        static_assert(sizeof(T) <= sizeof(misc_data_), "size of T too large");
        return *reinterpret_cast<T*>(static_cast<void*>(misc_data_));
    }

    void set_misc_data(const void* data_src, std::size_t data_size);

    qlog_forceinline SpscRingBuffer& get_buffer() {
        return *reinterpret_cast<SpscRingBuffer*>(buffer_);
    }

    qlog_forceinline void renew() {
        get_buffer().renew();
    }

    static constexpr std::ptrdiff_t get_buffer_data_offset() {
        return static_cast<std::ptrdiff_t>(
            (sizeof(block_node_head) + qlog::utility::QLOG_CACHE_LINE_SIZE - 1) -
            ((sizeof(block_node_head) + qlog::utility::QLOG_CACHE_LINE_SIZE - 1) %
             qlog::utility::QLOG_CACHE_LINE_SIZE));
    }

    qlog_forceinline block_list_type get_type() const {
        return type_;
    }

    qlog_forceinline void set_type(block_list_type type) {
        type_ = type;
    }
} QLOG_PACK_END

    QLOG_PACK_BEGIN class alignas(qlog::utility::QLOG_CACHE_LINE_SIZE) block_list {
    friend struct group_data_head;

   private:
    alignas(8) block_node_head::pointer_type head_;
    alignas(8) std::uint16_t offset_;
    alignas(8) std::uint16_t max_blocks_count_;
    alignas(8) std::size_t buffer_size_per_block_;
    alignas(8) const std::uint8_t* data_range_start_;
    alignas(8) const std::uint8_t* data_range_end_;
    alignas(8) block_list_type type_;

   private:
    void reset(std::uint16_t max_blocks_count, const std::uint8_t* buffers_base_addr,
               std::size_t blocks_total_buffer_size);

    bool try_recover_from_memory_map(std::uint16_t max_blocks_count,
                                     const std::uint8_t* buffers_base_addr,
                                     std::size_t blocks_total_buffer_size);

    void recovery_blocks();

   public:
    block_list(block_list_type type, std::uint16_t max_blocks_count,
               std::uint8_t* buffers_base_addr, std::size_t blocks_total_buffer_size,
               bool is_memory_recovery);

    ~block_list();

    qlog_forceinline block_list_type get_type() const {
        return type_;
    }

    qlog_forceinline block_node_head& get_block_head_by_index(std::uint16_t index) {
#if defined(QLOG_DEBUG)
        assert(index < max_blocks_count_ && "invalid block index!");
#endif
        return *reinterpret_cast<block_node_head*>(
            reinterpret_cast<std::uint8_t*>(this) +
            static_cast<std::ptrdiff_t>(*std::launder(&offset_)) +
            static_cast<std::ptrdiff_t>(*std::launder(&buffer_size_per_block_) * index));
    }

    qlog_forceinline std::uint16_t get_index_by_block_head(const block_node_head* block) const {
        if (!block) {
            return static_cast<std::uint16_t>(-1);
        }

        std::ptrdiff_t diff = reinterpret_cast<const std::uint8_t*>(block) -
                              (reinterpret_cast<const std::uint8_t*>(this) +
                               static_cast<std::ptrdiff_t>(*std::launder(&offset_)));

#if defined(QLOG_DEBUG)
        assert((static_cast<std::size_t>(diff) % buffer_size_per_block_ == 0) &&
               "invalid block node head address");
#endif
        return static_cast<std::uint16_t>(static_cast<std::size_t>(diff) / buffer_size_per_block_);
    }

    qlog_forceinline bool is_range_include(const block_node_head* block) const {
        if (!block) {
            return false;
        }

        return reinterpret_cast<const std::uint8_t*>(block) >= data_range_start_ &&
               reinterpret_cast<const std::uint8_t*>(block) < data_range_end_;
    }

#if defined(QLOG_DEBUG) || defined(QLOG_UNIT_TEST)
    qlog_forceinline bool is_include(const block_node_head* block_node) {
        if (!block_node) {
            return false;
        }

        std::uint16_t index = get_index_by_block_head(block_node);
        if (index >= max_blocks_count_) {
            return false;
        }

        block_node_head* test_node = first();
        while (test_node) {
            if (block_node == test_node) {
                return true;
            }
            test_node = next(test_node);
        }
        return false;
    }

    qlog_forceinline void debug_output() {
        auto block_iter = first();
        while (block_iter) {
            qlog::utility::util::log_device_console(qlog::LogLevel::error, "=>%" PRIu16,
                                                    get_index_by_block_head(block_iter));
            block_iter = next(block_iter);
        }

        qlog::utility::util::log_device_console(qlog::LogLevel::error, "\n");

        for (std::uint16_t i = 0; i < max_blocks_count_; ++i) {
            auto& block_head = get_block_head_by_index(i);
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error, "->%" PRIu8,
                static_cast<std::uint8_t>(block_head.get_type()));
        }
        std::fflush(stdout);
    }
#endif

    qlog_forceinline block_node_head* first() {
        auto head_union =
            BUFFER_ATOMIC_CAST_IGNORE_ALIGNMENT(head_.union_value(), std::uint32_t).load_acquire();

        block_node_head::pointer_type head_copy;
        head_copy.union_value() = head_union;

        if (head_copy.is_empty()) {
            return nullptr;
        }
        return &get_block_head_by_index(head_copy.index());
    }

    qlog_forceinline block_node_head* next(const block_node_head* current) {
        auto next_index = current->next_.index();
        if (block_node_head::pointer_type::is_empty_index(next_index)) {
            return nullptr;
        }
        return &get_block_head_by_index(next_index);
    }

    qlog_forceinline block_node_head* pop() {
        auto head_cpy =
            BUFFER_ATOMIC_CAST_IGNORE_ALIGNMENT(head_.union_value(), std::uint32_t).load_acquire();

        auto& head_cpy_ref =
            *reinterpret_cast<block_node_head::pointer_type*>(reinterpret_cast<char*>(&head_cpy));

        block_node_head::pointer_type head_desired;
        block_node_head* first_node = nullptr;

        do {
            if (head_cpy_ref.is_empty()) {
                return nullptr;
            }

            first_node = &get_block_head_by_index(head_cpy_ref.index());
            head_desired.index() = first_node->next_.index();
            head_desired.aba_mark() = static_cast<std::uint16_t>(head_cpy_ref.aba_mark() + 1);
        } while (!BUFFER_ATOMIC_CAST_IGNORE_ALIGNMENT(head_.union_value(), std::uint32_t)
                      .compare_exchange_strong(head_cpy, head_desired.union_value(),
                                               qlog::platform::memory_order::release,
                                               qlog::platform::memory_order::acquire));

        return first_node;
    }

    qlog_forceinline void push(block_node_head* new_block_node) {
#if defined(QLOG_DEBUG) || defined(QLOG_UNIT_TEST)
        if (is_include(new_block_node)) {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error, "push failed, new_block_node already in the list!");
            assert(false && "push failed, new_block_node already in the list!");
        }

        [[maybe_unused]] std::uint16_t new_index = get_index_by_block_head(new_block_node);
        assert(new_index < max_blocks_count_ && "push assert failed, invalid new_block_node!");
#endif
        new_block_node->set_type(type_);

        auto head_cpy =
            BUFFER_ATOMIC_CAST_IGNORE_ALIGNMENT(head_.union_value(), std::uint32_t).load_acquire();

        auto& head_cpy_ref =
            *reinterpret_cast<block_node_head::pointer_type*>(reinterpret_cast<char*>(&head_cpy));

        block_node_head::pointer_type head_desired;

        do {
            new_block_node->next_.index() = head_cpy_ref.index();
            head_desired.index() = get_index_by_block_head(new_block_node);
            head_desired.aba_mark() = static_cast<std::uint16_t>(head_cpy_ref.aba_mark() + 1);
        } while (!BUFFER_ATOMIC_CAST_IGNORE_ALIGNMENT(head_.union_value(), std::uint32_t)
                      .compare_exchange_strong(head_cpy, head_desired.union_value(),
                                               qlog::platform::memory_order::release,
                                               qlog::platform::memory_order::acquire));
    }

    qlog_forceinline void push_after_thread_unsafe(block_node_head* prev_block_node,
                                                   block_node_head* new_block_node) {
#if defined(QLOG_DEBUG) || defined(QLOG_UNIT_TEST)
        if (prev_block_node && prev_block_node->get_type() != type_) {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error,
                "push_after_thread_unsafe failed, prev_block_node type "
                "mismatch!, expected:%" PRIu8 ", but:%" PRIu8,
                static_cast<std::uint8_t>(type_),
                static_cast<std::uint8_t>(prev_block_node->get_type()));
            assert(false &&
                   "push_after_thread_unsafe failed, prev_block_node "
                   "type mismatch!");
        }

        if (new_block_node->get_type() == type_) {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error,
                "push_after_thread_unsafe failed, new_block_node already "
                "in the list!, type:%" PRIu8,
                static_cast<std::uint8_t>(new_block_node->get_type()));
            assert(false &&
                   "push_after_thread_unsafe failed, new_block_node "
                   "already in the list!");
        }

        [[maybe_unused]] std::uint16_t new_index = get_index_by_block_head(new_block_node);
        [[maybe_unused]] std::uint16_t prev_index = get_index_by_block_head(prev_block_node);

        assert(new_index < max_blocks_count_ &&
               "push_after_thread_unsafe assert failed, invalid new_block_node!");
        assert((prev_index == static_cast<std::uint16_t>(-1) || prev_index < max_blocks_count_) &&
               "push_after_thread_unsafe assert failed, invalid prev_block_node!");

        block_node_head* test_node = first();
        bool found_prev = false;
        while (test_node) {
            assert(test_node != new_block_node);
            found_prev |= prev_block_node == test_node;
            test_node = next(test_node);
        }
        assert(found_prev || !prev_block_node);
#endif
        new_block_node->set_type(type_);

        auto& prev_pointer = prev_block_node ? prev_block_node->next_ : head_;
        new_block_node->next_ = prev_pointer;

        prev_pointer.index() = static_cast<std::uint16_t>(
            (reinterpret_cast<const std::uint8_t*>(new_block_node) -
             (reinterpret_cast<const std::uint8_t*>(this) + static_cast<std::ptrdiff_t>(offset_))) /
            static_cast<std::ptrdiff_t>(buffer_size_per_block_));
    }

    qlog_forceinline bool remove_thread_unsafe(block_node_head* prev_block_node,
                                               block_node_head* remove_block_node) {
#if defined(QLOG_UNIT_TEST) || defined(QLOG_DEBUG)
        if (!is_include(remove_block_node)) {
            return false;
        }

        if (prev_block_node) {
            if (!is_include(prev_block_node)) {
                return false;
            }

            if (prev_block_node->get_type() != type_) {
                qlog::utility::util::log_device_console(
                    qlog::LogLevel::error,
                    "remove_thread_unsafe failed, prev_block_node type "
                    "mismatch!, expected:%" PRIu8 ", but:%" PRIu8,
                    static_cast<std::uint8_t>(type_),
                    static_cast<std::uint8_t>(prev_block_node->get_type()));
                assert(false &&
                       "remove_thread_unsafe failed, prev_block_node "
                       "type mismatch!");
            }
        }

        if (remove_block_node->get_type() != type_) {
            qlog::utility::util::log_device_console(
                qlog::LogLevel::error,
                "remove_thread_unsafe failed, remove_block_node type "
                "mismatch!, expected:%" PRIu8 ", but:%" PRIu8,
                static_cast<std::uint8_t>(type_),
                static_cast<std::uint8_t>(remove_block_node->get_type()));
            assert(false && "remove_thread_unsafe failed, prev_block_node type mismatch!");
        }

        bool check_prev = true;
        if (prev_block_node) {
            if (next(prev_block_node) != remove_block_node) {
                check_prev = false;
            }
        } else if (first() != remove_block_node) {
            check_prev = false;
        }

        if (!check_prev) {
            return false;
        }
#endif
        if (!prev_block_node) {
            head_ = remove_block_node->next_;
        } else {
            prev_block_node->next_ = remove_block_node->next_;
        }

        return true;
    }
} QLOG_PACK_END

    static_assert(sizeof(block_list) == qlog::utility::QLOG_CACHE_LINE_SIZE,
                  "invalid block_list size");

QLOG_SUPPRESS_MAYBE_UNINITIALIZED_END()

}  // namespace qlog::buffer