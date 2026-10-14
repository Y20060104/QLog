#pragma once

#include <cassert>
#include <cstdint>
#include <new>

#include "qlog/platform/aligned_memory.hpp"
#include "qlog/platform/atomic/atomic.hpp"
#include "qlog/platform/thread_ops.hpp"

namespace qlog {
template <typename P>
struct _global_vars_priority_var_initializer {
    static void init() {
        P::get();  // CR:get哪来的？
    }
};

template <>
struct _global_vars_priority_var_initializer<void> {
    static void init() {}
};

struct global_var_destructiable {
    friend struct global_vars_destructor;

   protected:
    virtual void partial_destruct() {}  // cR:partial_destruct()不用实现吗？
};

struct global_vars_destructor {
   public:
    void register_destructible_var(global_var_destructiable* var);
    ~global_vars_destructor();
};

global_vars_destructor& get_global_var_destructor();
// CR:这前面分别建立了哪些结构体？ 全局基础类型 可析构的类型
// ?atomic_trivially_constructible是之前实现的平凡可析构atomic？
template <typename T, typename Priority_Global_Var_Type = void>
struct global_vars_base : public global_var_destructiable {
   protected:
    static thread_local T* global_vars_ptr_;
    static qlog::platform::atomic_trivially_constructible<std::int64_t> global_vars_init_flag_;
    static T* global_vars_buffer_;
    static qlog::platform::atomic_trivially_constructible<T**> initializer_mark_;

   protected:
    virtual ~global_vars_base() {
        assert(false && "global_vars_base should not be directly destructed");
    }

   public:
    static T& get() {
        if (T::global_vars_ptr_) {
            return *T::global_vars_ptr_;
        }  // 如果有指针缓存就直接返回

        auto initializer_mark_value_tmp = initializer_mark_.load_acquire();
        if (&T::global_vars_ptr_ == initializer_mark_value_tmp &&
            initializer_mark_value_tmp != nullptr) {
            T::global_vars_ptr_ = T::global_vars_buffer_;
            return *T::global_vars_buffer_;
        }  // CR：这是什么情况？

        if (!T::global_vars_ptr_) {
            if (T::global_vars_init_flag_.load_acquire() == 0) {
                std::int64_t expected = 0;

                if (T::global_vars_init_flag_.compare_exchange_strong(
                        expected, 1, qlog::platform::memory_order::release,
                        qlog::platform::memory_order::acquire)) {
                    initializer_mark_.store_release(&T::global_vars_ptr_);

                    _global_vars_priority_var_initializer<Priority_Global_Var_Type>::init();

                    T::global_vars_buffer_ =
                        static_cast<T*>(qlog::platform::aligned_alloc(alignof(T), sizeof(T)));
                    T::global_vars_ptr_ = T::global_vars_buffer_;

                    ::new (static_cast<void*>(T::global_vars_buffer_)) T();

                    get_global_var_destructor().register_destructible_var(T::global_vars_ptr_);

                    T::global_vars_init_flag_.store_release(2);  // CR:为什么要存2？
                    return *T::global_vars_ptr_;
                }
            }  // 这是创建TLS指针花村

            while (T::global_vars_init_flag_.load_acquire() != 2) {
                qlog::platform::yield();
            }  // CR:这是在干什么？ 为什么需要yield

            T::global_vars_ptr_ = T::global_vars_buffer_;
        }
        return *T::global_vars_ptr_;
    }
};

template <typename T, typename Priority_Global_Var_Type>
thread_local T* global_vars_base<T, Priority_Global_Var_Type>::global_vars_ptr_;

template <typename T, typename Priority_Global_Var_Type>
qlog::platform::atomic_trivially_constructible<std::int64_t>
    global_vars_base<T, Priority_Global_Var_Type>::global_vars_init_flag_;

template <typename T, typename Priority_Global_Var_Type>
T* global_vars_base<T, Priority_Global_Var_Type>::global_vars_buffer_;

template <typename T, typename Priority_Global_Var_Type>
qlog::platform::atomic_trivially_constructible<T**>
    global_vars_base<T, Priority_Global_Var_Type>::initializer_mark_;

// CR:这个四个命名和class内命名好像一样？

}  // namespace qlog