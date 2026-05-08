#pragma once
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace qlog
{

// 全局分配接口
void* aligned_alloc(size_t alignment, size_t size);
void aligned_free(void* ptr);

// STL 兼容分配器
template<typename T, size_t Alignment = 64> class aligned_allocator
{
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using size_type = size_t;
    using difference_type = std::ptrdiff_t;

    template<typename U> struct rebind
    {
        using other = aligned_allocator<U, Alignment>;
    };

    using propagate_on_container_move_assignment = std::true_type;
    using is_always_equal = std::true_type;

    aligned_allocator() = default;
    ~aligned_allocator() = default;

    // 转换构造（允许不同类型的分配器互相转换）
    template<typename U, size_t OtherAlignment>
    aligned_allocator(const aligned_allocator<U, OtherAlignment>&)
    {
    }

    // STL 必需的分配方法
    pointer allocate(size_t n)
    {
        // aligned_alloc 参数: (alignment, size_in_bytes)
        return reinterpret_cast<pointer>(aligned_alloc(Alignment, n * sizeof(T)));
    }

    void deallocate(pointer p, size_t n)
    {
        (void)n; // 未使用但需要匹配 STL 签名
        aligned_free(p);
    }

    // 比较运算符
    bool operator==(const aligned_allocator&) const noexcept
    {
        return true;
    }

    bool operator!=(const aligned_allocator&) const noexcept
    {
        return false;
    }
};

} // namespace qlog