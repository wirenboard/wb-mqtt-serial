#pragma once

#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>
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

    /**
     * same as std::remove_pointer but works for all types with dereference operator
     */
    template <typename T>
    struct dereferenced_type
    {
        using type = decltype(*T());
    };

    template <typename Pointer>
    struct ptr_hash
    {
        using T = std::decay<dereferenced_type<Pointer>::type>::type;

        inline size_t operator()(const Pointer & ptr) const
        {
            if (!ptr) return std::hash<Pointer>()(ptr);

            return T::hash()(*ptr);
        }
    };

    template <typename T>
    struct weak_ptr_hash
    {
        inline size_t operator()(const std::weak_ptr<T> & wptr) const
        {
            auto ptr = wptr.lock();
            return ptr_hash<decltype(ptr)>()(ptr);
        }
    };
}

template <typename K>
using TPSet = std::set<std::shared_ptr<K>, utils::ptr_cmp<K>>;

template <typename K, typename V>
using TPMap = std::map<std::shared_ptr<K>, V, utils::ptr_cmp<K>>;

template <typename K>
using TPUnorderedSet = std::unordered_set<std::shared_ptr<K>, utils::ptr_hash<K>>;

template <typename K, typename V>
using TPUnorderedMap = std::unordered_map<std::shared_ptr<K>, V, utils::ptr_hash<K>>;

template <typename K, typename V>
TPSet<K> GetKeysAsSet(const TPMap<K, V> & map)
{
    TPSet<K> keys;

    std::transform(map.begin(), map.end(),
        std::inserter(keys, keys.end()),
        [](const std::pair<K, V> & item) {
            return item.first;
        }
    );

    return keys;
}

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
