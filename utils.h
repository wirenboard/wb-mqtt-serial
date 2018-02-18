#pragma once

#include <memory>
#include <type_traits>

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

template <typename K>
using TPSet = std::set<std::shared_ptr<K>, utils::ptr_cmp<K>>;

template <typename K, typename V>
using TPMap = std::map<std::shared_ptr<K>, V, utils::ptr_cmp<K>>;

template <typename T>
struct _mutable
{
    static_assert(std::is_fundamental<T>::value, "'_mutable' wrapper should be used only with fundamental types");

    mutable T Value;

    _mutable() = default;
    _mutable(const T & value)
        : Value(value)
    {}

    T operator=(const T & value) const
    {
        Value = value;
    }

    operator T() const
    {
        return Value;
    }
};
