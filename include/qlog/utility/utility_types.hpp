#pragma once

#include <cstddef>
#include <cstdint>

#include "qlog/platform/aligned_memory.hpp"
#include "qlog/utility/type_tools.hpp"

namespace qlog::utility {
static constexpr std::size_t QLOG_CACHE_LINE_SIZE = 128;
static constexpr std::size_t QLOG_CACHE_LINE_SIZE_LOG2 = 7;

template <typename T>
class shared_ptr {
   public:
    shared_ptr() noexcept : ptr_(nullptr), ref_count_(nullptr) {}

    shared_ptr(const shared_ptr& rhs) noexcept {
        if (rhs.ref_count_) {
            std::int64_t prev_value = rhs.ref_count_->fetch_add_seq_cst(1);
            if ((prev_value >> 32) == 0) {
                ptr_ = rhs.ptr_;
                ref_count_ = rhs.ref_count_;
                return;
            }
        }
        ptr_ = nullptr;
        ref_count_ = nullptr;
    }

    template <typename D>
    shared_ptr(shared_ptr<D>&& rhs) noexcept : ptr_(rhs.ptr_), ref_count_(rhs.ref_count_) {
        rhs.ptr_ = nullptr;
        rhs.ref_count_ = nullptr;
    }

    ~shared_ptr() {
        release();
    }

    shared_ptr& operator=(const shared_ptr& rhs) {
        if (this != &rhs) {
            release();
            if (rhs.ref_count_) {
                std::int64_t prev_value = rhs.ref_count_->fetch_add_seq_cst(1);
                if ((prev_value >> 32) == 0) {
                    ptr_ = rhs.ptr_;
                    ref_count_ = rhs.ref_count_;
                    return *this;
                }
            }
        }
        return *this;
    }

    template <typename D>
    shared_ptr& operator=(shared_ptr<D>&& rhs) noexcept {
        if (this != &rhs) {
            release();
            ptr_ = rhs.ptr_;
            ref_count_ = rhs.ref_count_;
            rhs.ptr_ = nullptr;
            rhs.ref_count_ = nullptr;
        }
        return *this;
    }

    T* get() const noexcept {
        return ptr_;
    }

    T* operator->() const {
        return get();
    }

    T& operator*() const {
        return *get();
    }

    std::int32_t use_count() const noexcept {
        if (ref_count_) {
            std::int64_t state = ref_count_->load_relaxed();
            if ((state >> 32) == 0) {
                return static_cast<std::int32_t>(state & 0xFFFFFFFF);
            }
        }
        return 0;
    }

    template <typename D>
    bool operator==(const shared_ptr<D>& rhs) const noexcept {
        return ptr_ == rhs.ptr_;
    }

    bool operator==(decltype(nullptr)) const noexcept {
        return ptr_ == nullptr;
    }

    template <typename D>
    bool operator!=(const shared_ptr<D>& rhs) const noexcept {
        return ptr_ != rhs.ptr_;
    }

    template <typename D>
    bool operator!=(decltype(nullptr)) const noexcept {
        return ptr_ != nullptr;
    }

    void reset() noexcept {
        release();
    }

    void reset(T* new_ptr) {
        release();
        ptr_ = new_ptr;
        ref_count_ = new_ptr ? new qlog::platform::atomic<std::int64_t>(1) : nullptr;
    }

   private:
    template <typename>
    friend class shared_ptr;

    template <typename U, typename... Args>
    friend shared_ptr<U> make_shared(Args&&...);

    template <typename U>
    friend shared_ptr<U> make_shared();

    explicit shared_ptr(T* new_ptr)
        : ptr_(new_ptr),
          ref_count_(new_ptr ? new qlog::platform::atomic<std::int64_t>(1) : nullptr) {}

    void release() noexcept {
        if (ref_count_) {
            std::int64_t expected_value = ref_count_->load_relaxed();

            while (true) {
                if (expected_value == 1) {
                    if (ref_count_->compare_exchange_strong(
                            expected_value, static_cast<std::int64_t>(1) << 32,
                            qlog::platform::memory_order::seq_cst,
                            qlog::platform::memory_order::seq_cst)) {
                        ptr_->~T();
                        qlog::platform::aligned_free(ptr_);
                        delete ref_count_;
                        break;
                    }
                } else {
                    if (ref_count_->compare_exchange_strong(
                            expected_value, expected_value - 1,
                            qlog::platform::memory_order::seq_cst,
                            qlog::platform::memory_order::seq_cst)) {
                        break;
                    }
                }
            }
        }

        ptr_ = nullptr;
        ref_count_ = nullptr;
    }

    T* ptr_;
    qlog::platform::atomic<std::int64_t>* ref_count_;
};

template <typename T, typename... Ts>
shared_ptr<T> make_shared(Ts&&... params) {
    void* addr = qlog::platform::aligned_alloc(alignof(T), sizeof(T));

    new (addr, qlog::utility::enum_new_dummy::dummy) T(qlog::utility::forward<Ts>(params)...);

    return shared_ptr<T>(static_cast<T*>(addr));
}

template <typename T>
shared_ptr<T> make_shared() {
    void* addr = qlog::platform::aligned_alloc(alignof(T), sizeof(T));

    new (addr, qlog::utility::enum_new_dummy::dummy) T();

    return shared_ptr<T>(static_cast<T*>(addr));
}

// ref_count_为什么是指针？
}  // namespace qlog::utility