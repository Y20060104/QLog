#pragma once

#include <cstddef>

#include "qlog/buffer/log_buffer_defs.hpp"
#include "qlog/platform/spin_lock.hpp"
#include "qlog/utility/type_traits.hpp"
#include "qlog/utility/utility_types.hpp"

namespace qlog::buffer {

template <typename T>
class memory_pool;

template <typename T, bool Align>
class memory_pool_obj_base;

template <typename T>
class alignas(qlog::utility::QLOG_CACHE_LINE_SIZE) memory_pool_obj_base<T, true> {
   public:
    static constexpr bool is_aligned = true;

   private:
    friend memory_pool<T>;

    T* memory_pool_next_;
    T* memory_pool_prev_;
    char memory_pool_padding_[qlog::utility::QLOG_CACHE_LINE_SIZE - 2 * sizeof(T*)];
};

static_assert(sizeof(memory_pool_obj_base<char, true>) == qlog::utility::QLOG_CACHE_LINE_SIZE,
              "invalid memory_pool_obj_base struct size");

static_assert(qlog::utility::is_pod<memory_pool_obj_base<char, true>>::value,
              "invalid memory_pool_obj_base struct type");

template <typename T>
class memory_pool_obj_base<T, false> {
   public:
    static constexpr bool is_aligned = false;

   private:
    friend memory_pool<T>;

    T* memory_pool_next_;
    T* memory_pool_prev_;
};

static_assert(sizeof(memory_pool_obj_base<char, false>) == 2 * sizeof(void*),
              "invalid memory_pool_obj_base struct size");

static_assert(qlog::utility::is_pod<memory_pool_obj_base<char, false>>::value,
              "invalid memory_pool_obj_base struct type");

template <typename T>
class memory_pool {
   private:
    qlog::platform::spin_lock lock_;
    T* head_;
    T* tail_;

    static_assert(qlog::utility::is_base_of<memory_pool_obj_base<T, T::is_aligned>, T>::value,
                  "T must inherit from memory_pool_obj_base");

   public:
    memory_pool() : head_(nullptr), tail_(nullptr) {}

    qlog_forceinline void push(T* object) {
        if (!object) {
            return;
        }

        qlog::platform::scoped_spin_lock lock(lock_);

        object->memory_pool_next_ = head_;
        object->memory_pool_prev_ = nullptr;

        if (head_) {
            head_->memory_pool_prev_ = object;
        }

        head_ = object;

        if (!tail_) {
            tail_ = object;
        }
    }

    qlog_forceinline T* pop() {
        qlog::platform::scoped_spin_lock lock(lock_);

        if (!head_) {
            return nullptr;
        }

        T* old_head = head_;
        head_ = old_head->memory_pool_next_;

        if (head_) {
            head_->memory_pool_prev_ = nullptr;
        } else {
            tail_ = nullptr;
        }

        return old_head;
    }

    qlog_forceinline T* evict(bool (*evaluate)(const T*, void* user_data), void* user_data) {
        qlog::platform::scoped_spin_lock lock(lock_);

        if (!tail_) {
            return nullptr;
        }

        if (!evaluate(tail_, user_data)) {
            return nullptr;
        }

        T* old_tail = tail_;
        tail_ = old_tail->memory_pool_prev_;

        if (tail_) {
            tail_->memory_pool_next_ = nullptr;
        } else {
            head_ = nullptr;
        }

        return old_tail;
    }
    qlog_forceinline std::size_t size() {
        qlog::platform::scoped_spin_lock lock(lock_);

        std::size_t result = 0;
        T* obj = head_;

        while (obj) {
            ++result;
            obj = obj->memory_pool_next_;
        }

        return result;
    }
};
}  // namespace qlog::buffer