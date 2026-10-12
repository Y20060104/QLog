#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#ifdef __clang__
#define QLOG_CLANG 1
#elif defined(_MSC_VER)
#define QLOG_MSVC 1
#elif defined(__GNUC__)
#define QLOG_GCC 1
#endif

#ifdef QLOG_MSVC
#define qlog_forceinline __forceinline
#elif defined(QLOG_GCC) || defined(QLOG_CLANG)
#define qlog_forceinline __inline__ __attribute__((always_inline))
#else
#define qlog_forceinline inline
#endif

namespace qlog::platform {
enum class memory_order : std::uint8_t { relaxed, acquire, release, acq_rel, seq_cst };
}

#ifdef QLOG_MSVC
#include "qlog/platform/atomic/_inner_atomic_msvc.h"  //CR：有三个hpp不是h吧？
#elif defined(QLOG_GCC) || defined(QLOG_CLANG)
#include "qlog/platform/atomic/_inner_atomic_GNU_C.h"
#else
static_assert(false, "qlog::platform::atomic is not supported in your compiler");
#endif

namespace qlog::platform {

template <typename T>
class atomic_trivially_constructible : public _atomic_base<T, sizeof(T)> {
   public:
    typedef _atomic_base<T, sizeof(T)> base_type;
    static_assert(base_type::supported, "atomic type is not supported on this platform!");
    typedef typename std::decay<T>::type value_type;

    qlog_forceinline value_type load(memory_order order = memory_order::seq_cst) const noexcept {
        return base_type::load(order);
    }

    qlog_forceinline value_type load_raw() const noexcept {
        return base_type::load_raw();
    }

    qlog_forceinline value_type load_acquire() const noexcept {
        return base_type::load_acquire();
    }

    qlog_forceinline value_type load_relaxed() const noexcept {
        return base_type::load_relaxed();
    }

    qlog_forceinline value_type load_acq_rel() const noexcept {
        return base_type::load_acq_rel();
    }

    qlog_forceinline value_type load_seq_cst() const noexcept {
        return base_type::load_seq_cst();
    }

    qlog_forceinline void store(value_type value, memory_order order) noexcept {
        base_type::store(value, order);
    }

    qlog_forceinline void store_raw(value_type value) noexcept {
        base_type::store_raw(value);
    }

    qlog_forceinline void store_release(value_type value) noexcept {
        base_type::store_release(value);
    }

    qlog_forceinline void store_relaxed(value_type value) noexcept {
        base_type::store_relaxed(value);
    }

    qlog_forceinline void store_acq_rel(value_type value) noexcept {
        base_type::store_acq_rel(value);
    }

    qlog_forceinline void store_seq_cst(value_type value) noexcept {
        base_type::store_seq_cst(value);
    }

    qlog_forceinline value_type exchange(value_type value, memory_order order) noexcept {
        return base_type::exchange(value, order);
    }

    qlog_forceinline value_type exchange_raw(value_type value) noexcept {
        return base_type::exchange_raw(value);
    }
    qlog_forceinline value_type exchange_acquire(value_type value) noexcept {
        return base_type::exchange_acquire(value);
    }

    qlog_forceinline value_type exchange_release(value_type value) noexcept {
        return base_type::exchange_release(value);
    }

    qlog_forceinline value_type exchange_relaxed(value_type value) noexcept {
        return base_type::exchange_relaxed(value);
    }

    qlog_forceinline value_type exchange_acq_rel(value_type value) noexcept {
        return base_type::exchange_acq_rel(value);
    }

    qlog_forceinline value_type exchange_seq_cst(value_type value) noexcept {
        return base_type::exchange_seq_cst(value);
    }

    qlog_forceinline bool compare_exchange_weak(
        value_type& expected, value_type desired,
        const memory_order success_order = memory_order::seq_cst,
        const memory_order fail_order = memory_order::seq_cst) noexcept {
        return base_type::compare_exchange_weak(expected, desired, success_order, fail_order);
    }

    qlog_forceinline bool compare_exchange_strong(
        value_type& expected, value_type desired,
        const memory_order success_order = memory_order::seq_cst,
        const memory_order fail_order = memory_order::seq_cst) noexcept {
        return base_type::compare_exchange_strong(expected, desired, success_order, fail_order);
    }
    qlog_forceinline value_type add_fetch(value_type val, memory_order order) noexcept {
        return base_type::add_fetch(val, order);
    }

    qlog_forceinline value_type add_fetch_raw(value_type val) noexcept {
        return base_type::add_fetch_raw(val);
    }

    qlog_forceinline value_type add_fetch_acquire(value_type val) noexcept {
        return base_type::add_fetch_acquire(val);
    }

    qlog_forceinline value_type add_fetch_release(value_type val) noexcept {
        return base_type::add_fetch_release(val);
    }

    qlog_forceinline value_type add_fetch_relaxed(value_type val) noexcept {
        return base_type::add_fetch_relaxed(val);
    }

    qlog_forceinline value_type add_fetch_acq_rel(value_type val) noexcept {
        return base_type::add_fetch_acq_rel(val);
    }

    qlog_forceinline value_type add_fetch_seq_cst(value_type val) noexcept {
        return base_type::add_fetch_seq_cst(val);
    }

    // fetch_add
    qlog_forceinline value_type fetch_add(value_type val, memory_order order) noexcept {
        return base_type::fetch_add(val, order);
    }

    qlog_forceinline value_type fetch_add_raw(value_type val) noexcept {
        return base_type::fetch_add_raw(val);
    }

    qlog_forceinline value_type fetch_add_acquire(value_type val) noexcept {
        return base_type::fetch_add_acquire(val);
    }

    qlog_forceinline value_type fetch_add_release(value_type val) noexcept {
        return base_type::fetch_add_release(val);
    }

    qlog_forceinline value_type fetch_add_relaxed(value_type val) noexcept {
        return base_type::fetch_add_relaxed(val);
    }

    qlog_forceinline value_type fetch_add_acq_rel(value_type val) noexcept {
        return base_type::fetch_add_acq_rel(val);
    }

    qlog_forceinline value_type fetch_add_seq_cst(value_type val) noexcept {
        return base_type::fetch_add_seq_cst(val);
    }

    // sub_fetch
    qlog_forceinline value_type sub_fetch(value_type val, memory_order order) noexcept {
        return base_type::sub_fetch(val, order);
    }

    qlog_forceinline value_type sub_fetch_raw(value_type val) noexcept {
        return base_type::sub_fetch_raw(val);
    }

    qlog_forceinline value_type sub_fetch_acquire(value_type val) noexcept {
        return base_type::sub_fetch_acquire(val);
    }

    qlog_forceinline value_type sub_fetch_release(value_type val) noexcept {
        return base_type::sub_fetch_release(val);
    }

    qlog_forceinline value_type sub_fetch_relaxed(value_type val) noexcept {
        return base_type::sub_fetch_relaxed(val);
    }

    qlog_forceinline value_type sub_fetch_acq_rel(value_type val) noexcept {
        return base_type::sub_fetch_acq_rel(val);
    }

    qlog_forceinline value_type sub_fetch_seq_cst(value_type val) noexcept {
        return base_type::sub_fetch_seq_cst(val);
    }

    // fetch_sub
    qlog_forceinline value_type fetch_sub(value_type val, memory_order order) noexcept {
        return base_type::fetch_sub(val, order);
    }

    qlog_forceinline value_type fetch_sub_raw(value_type val) noexcept {
        return base_type::fetch_sub_raw(val);
    }

    qlog_forceinline value_type fetch_sub_acquire(value_type val) noexcept {
        return base_type::fetch_sub_acquire(val);
    }

    qlog_forceinline value_type fetch_sub_release(value_type val) noexcept {
        return base_type::fetch_sub_release(val);
    }

    qlog_forceinline value_type fetch_sub_relaxed(value_type val) noexcept {
        return base_type::fetch_sub_relaxed(val);
    }

    qlog_forceinline value_type fetch_sub_acq_rel(value_type val) noexcept {
        return base_type::fetch_sub_acq_rel(val);
    }

    qlog_forceinline value_type fetch_sub_seq_cst(value_type val) noexcept {
        return base_type::fetch_sub_seq_cst(val);
    }

    // xor_fetch
    qlog_forceinline value_type xor_fetch(value_type val, memory_order order) noexcept {
        return base_type::xor_fetch(val, order);
    }

    qlog_forceinline value_type xor_fetch_raw(value_type val) noexcept {
        return base_type::xor_fetch_raw(val);
    }

    qlog_forceinline value_type xor_fetch_acquire(value_type val) noexcept {
        return base_type::xor_fetch_acquire(val);
    }

    qlog_forceinline value_type xor_fetch_release(value_type val) noexcept {
        return base_type::xor_fetch_release(val);
    }

    qlog_forceinline value_type xor_fetch_relaxed(value_type val) noexcept {
        return base_type::xor_fetch_relaxed(val);
    }

    qlog_forceinline value_type xor_fetch_acq_rel(value_type val) noexcept {
        return base_type::xor_fetch_acq_rel(val);
    }

    qlog_forceinline value_type xor_fetch_seq_cst(value_type val) noexcept {
        return base_type::xor_fetch_seq_cst(val);
    }

    // fetch_xor
    qlog_forceinline value_type fetch_xor(value_type val, memory_order order) noexcept {
        return base_type::fetch_xor(val, order);
    }

    qlog_forceinline value_type fetch_xor_raw(value_type val) noexcept {
        return base_type::fetch_xor_raw(val);
    }

    qlog_forceinline value_type fetch_xor_acquire(value_type val) noexcept {
        return base_type::fetch_xor_acquire(val);
    }

    qlog_forceinline value_type fetch_xor_release(value_type val) noexcept {
        return base_type::fetch_xor_release(val);
    }

    qlog_forceinline value_type fetch_xor_relaxed(value_type val) noexcept {
        return base_type::fetch_xor_relaxed(val);
    }

    qlog_forceinline value_type fetch_xor_acq_rel(value_type val) noexcept {
        return base_type::fetch_xor_acq_rel(val);
    }

    qlog_forceinline value_type fetch_xor_seq_cst(value_type val) noexcept {
        return base_type::fetch_xor_seq_cst(val);
    }

    // or_fetch
    qlog_forceinline value_type or_fetch(value_type val, memory_order order) noexcept {
        return base_type::or_fetch(val, order);
    }

    qlog_forceinline value_type or_fetch_raw(value_type val) noexcept {
        return base_type::or_fetch_raw(val);
    }

    qlog_forceinline value_type or_fetch_acquire(value_type val) noexcept {
        return base_type::or_fetch_acquire(val);
    }

    qlog_forceinline value_type or_fetch_release(value_type val) noexcept {
        return base_type::or_fetch_release(val);
    }

    qlog_forceinline value_type or_fetch_relaxed(value_type val) noexcept {
        return base_type::or_fetch_relaxed(val);
    }

    qlog_forceinline value_type or_fetch_acq_rel(value_type val) noexcept {
        return base_type::or_fetch_acq_rel(val);
    }

    qlog_forceinline value_type or_fetch_seq_cst(value_type val) noexcept {
        return base_type::or_fetch_seq_cst(val);
    }

    // fetch_or
    qlog_forceinline value_type fetch_or(value_type val, memory_order order) noexcept {
        return base_type::fetch_or(val, order);
    }

    qlog_forceinline value_type fetch_or_raw(value_type val) noexcept {
        return base_type::fetch_or_raw(val);
    }

    qlog_forceinline value_type fetch_or_acquire(value_type val) noexcept {
        return base_type::fetch_or_acquire(val);
    }

    qlog_forceinline value_type fetch_or_release(value_type val) noexcept {
        return base_type::fetch_or_release(val);
    }

    qlog_forceinline value_type fetch_or_relaxed(value_type val) noexcept {
        return base_type::fetch_or_relaxed(val);
    }

    qlog_forceinline value_type fetch_or_acq_rel(value_type val) noexcept {
        return base_type::fetch_or_acq_rel(val);
    }

    qlog_forceinline value_type fetch_or_seq_cst(value_type val) noexcept {
        return base_type::fetch_or_seq_cst(val);
    }

    // and_fetch
    qlog_forceinline value_type and_fetch(value_type val, memory_order order) noexcept {
        return base_type::and_fetch(val, order);
    }

    qlog_forceinline value_type and_fetch_raw(value_type val) noexcept {
        return base_type::and_fetch_raw(val);
    }

    qlog_forceinline value_type and_fetch_acquire(value_type val) noexcept {
        return base_type::and_fetch_acquire(val);
    }

    qlog_forceinline value_type and_fetch_release(value_type val) noexcept {
        return base_type::and_fetch_release(val);
    }

    qlog_forceinline value_type and_fetch_relaxed(value_type val) noexcept {
        return base_type::and_fetch_relaxed(val);
    }

    qlog_forceinline value_type and_fetch_acq_rel(value_type val) noexcept {
        return base_type::and_fetch_acq_rel(val);
    }

    qlog_forceinline value_type and_fetch_seq_cst(value_type val) noexcept {
        return base_type::and_fetch_seq_cst(val);
    }

    // fetch_and
    qlog_forceinline value_type fetch_and(value_type val, memory_order order) noexcept {
        return base_type::fetch_and(val, order);
    }

    qlog_forceinline value_type fetch_and_raw(value_type val) noexcept {
        return base_type::fetch_and_raw(val);
    }

    qlog_forceinline value_type fetch_and_acquire(value_type val) noexcept {
        return base_type::fetch_and_acquire(val);
    }

    qlog_forceinline value_type fetch_and_release(value_type val) noexcept {
        return base_type::fetch_and_release(val);
    }

    qlog_forceinline value_type fetch_and_relaxed(value_type val) noexcept {
        return base_type::fetch_and_relaxed(val);
    }

    qlog_forceinline value_type fetch_and_acq_rel(value_type val) noexcept {
        return base_type::fetch_and_acq_rel(val);
    }

    qlog_forceinline value_type fetch_and_seq_cst(value_type val) noexcept {
        return base_type::fetch_and_seq_cst(val);
    }

    qlog_forceinline atomic_trivially_constructible<T>& operator++() {
        fetch_add_seq_cst(1);
        return *this;
    }

    qlog_forceinline atomic_trivially_constructible<T> operator++(std::int32_t) {
        atomic_trivially_constructible<T> tmp = *this;
        fetch_add_seq_cst(1);
        return tmp;
    }

    qlog_forceinline atomic_trivially_constructible<T>& operator--() {
        fetch_sub_seq_cst(1);
        return *this;
    }

    qlog_forceinline atomic_trivially_constructible<T> operator--(std::int32_t) {
        atomic_trivially_constructible<T> tmp = *this;
        fetch_sub_seq_cst(1);
        return tmp;
    }

    qlog_forceinline atomic_trivially_constructible<T>& operator+=(value_type value) {
        fetch_add_seq_cst(value);
        return *this;
    }

    qlog_forceinline atomic_trivially_constructible<T> operator+(value_type value) {
        atomic_trivially_constructible<T> tmp = *this;
        tmp.add_fetch_seq_cst(value);
        return tmp;
    }

    qlog_forceinline atomic_trivially_constructible<T>& operator-=(value_type value) {
        fetch_sub_seq_cst(value);
        return *this;
    }

    qlog_forceinline atomic_trivially_constructible<T> operator-(value_type value) {
        atomic_trivially_constructible<T> tmp = *this;
        tmp.sub_fetch_seq_cst(value);
        return tmp;
    }
};
// CR:include/qlog/platform/atomic.hpp 改为include/qlog/platform/atomic/atomic.hpp会不会更合理？

template <typename T>
class atomic : public atomic_trivially_constructible<T> {
   public:
    typedef typename atomic_trivially_constructible<T>::base_type base_type;
    typedef typename atomic_trivially_constructible<T>::value_type value_type;
    typedef atomic_trivially_constructible<T>
        trivially_constructible_type;  // CR:这行是什么意思？ 名称是什么意思？

   public:
    atomic() {
        base_type::explicit_copy(value_type());
    }

    atomic(const value_type& value) {
        base_type::explicit_copy(value);
    }

    atomic(const atomic<T>& rhs) {
        base_type::explicit_copy(rhs.load_seq_cst());  // CR:explicit copy是什么？
    }

    atomic(const trivially_constructible_type& rhs) {
        base_type::explicit_copy(rhs.load_seq_cst());
    }

    qlog_forceinline atomic<T>& operator=(const value_type& value) {
        this->store_seq_cst(value);
        return *this;
    }  // 这个是自定义atomic的赋值吗？

    qlog_forceinline atomic<T>& operator=(const atomic<T>& rhs) {
        this->store_seq_cst(rhs.load_seq_cst());
        return *this;  // CR:这个atomic=atomic的赋值吗？
    }

    qlog_forceinline atomic<T>& operator=(const trivially_constructible_type& rhs) {
        this->store_seq_cst(rhs.load_seq_cst());
        return *this;
    }

    qlog_forceinline atomic<T>& operator++() {
        this->fetch_add_seq_cst(1);
        return *this;
    }

    qlog_forceinline atomic<T> operator++(std::int32_t) {
        atomic<T> tmp = *this;
        this->fetch_add_seq_cst(1);
        return tmp;
    }

    qlog_forceinline atomic<T>& operator--() {
        this->fetch_sub_seq_cst(1);
        return *this;
    }

    qlog_forceinline atomic<T> operator--(std::int32_t) {
        atomic<T> tmp = *this;
        this->fetch_sub_seq_cst(1);
        return tmp;
    }

    qlog_forceinline atomic<T>& operator+=(value_type value) {
        this->fetch_add_seq_cst(value);
        return *this;
    }

    qlog_forceinline atomic<T> operator+(value_type value) {
        atomic<T> tmp = *this;
        tmp.add_fetch_seq_cst(value);
        return tmp;
    }

    qlog_forceinline atomic<T>& operator-=(value_type value) {
        this->fetch_sub_seq_cst(value);
        return *this;
    }

    qlog_forceinline atomic<T> operator-(value_type value) {
        atomic<T> tmp = *this;
        tmp.sub_fetch_seq_cst(value);
        return tmp;
    }
};

static_assert(std::is_trivially_constructible<atomic_trivially_constructible<std::int32_t>>::value,
              "atomic_trivially_constructible must be trivial type");
static_assert(
    std::is_trivially_copy_assignable<atomic_trivially_constructible<std::int32_t>>::value,
    "atomic_trivially_constructible must be trivial type");
static_assert(std::is_trivially_destructible<atomic_trivially_constructible<std::int32_t>>::value,
              "atomic_trivially_constructible must be trivial type");
static_assert(
    std::is_trivially_copy_constructible<atomic_trivially_constructible<std::int32_t>>::value,
    "atomic_trivially_constructible must be trivial type");
static_assert(
    std::is_trivially_move_assignable<atomic_trivially_constructible<std::int32_t>>::value,
    "atomic_trivially_constructible must be trivial type");
static_assert(
    std::is_trivially_move_constructible<atomic_trivially_constructible<std::int32_t>>::value,
    "atomic_trivially_constructible must be trivial type");
static_assert(!std::is_trivially_constructible<atomic<std::int32_t>>::value,
              "atomic must not be trivially constructible");
}  // namespace qlog::platform