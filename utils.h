#pragma once

#include <memory>

namespace utils
{
    template <typename Pointer>
    struct ptr_cmp
    {
        inline bool operator()(const Pointer & lptr, const Pointer & rptr) const
        {
            if (!rptr) return false; // nothing after expired pointer
            if (!lptr) return true;  // every not expired after expired pointer
            return *lptr < *rptr;
        }
    };

    template <typename T>
    struct weak_ptr_cmp
    {
        inline bool operator()(const std::weak_ptr<T> & lhs, const std::weak_ptr<T> & rhs) const
        {
            auto lptr = lhs.lock(), rptr = rhs.lock();
            return ptr_cmp<decltype(lptr)>()(lptr, rptr);
        }
    };
}

template <typename T>
using TPSet = std::set<std::shared_ptr<T>, utils::ptr_cmp<T>>;
