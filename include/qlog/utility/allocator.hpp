#pragma once

#include <cstdlib>

#include "qlog/utility/type_tools.hpp"

namespace qlog::utility {

template <typename Allocator>
class allocator_traits {
   private:
    template <typename A>
    struct extract_allocator_template;

    template <template <typename> class Wrapper, typename Inner>
    struct extract_allocator_template<Wrapper<Inner>> {
        template <typename U>
        using rebind = Wrapper<U>;
    };

   public:
    template <typename NewType>
    using rebind_alloc = typename extract_allocator_template<Allocator>::template rebind<NewType>;
};

template <typename T>
class default_allocator {
   public:
    using value_type = T;

    default_allocator() = default;

    template <typename U>
    default_allocator(const default_allocator<U>&) {}

    value_type* allocate(std::size_t n) {
        return static_cast<value_type*>(std::malloc(n * sizeof(value_type)));
    }

    void deallocate(value_type* p, std::size_t) {
        std::free(p);
    }

    template <typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        qlog::utility::object_constructor<U>::construct(p, qlog::utility::forward<Args>(args)...);
    }

    template <typename U>
    void destroy(U* p) {
        qlog::utility::object_destructor<U>::destruct(p);
    }

    template <typename U>
    void destroy(U* p, std::size_t n) {
        qlog::utility::object_array_destructor<U>::destruct(p, n);
    }

    bool operator==(const default_allocator&) const noexcept {
        return true;
    }

    bool operator!=(const default_allocator&) const noexcept {
        return false;
    }
};

}  // namespace qlog::utility