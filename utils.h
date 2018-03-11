#pragma once

#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <type_traits>

namespace utils
{
    template < template <typename...> class Template, typename T >
    struct is_instantiation_of : std::false_type {};

    template < template <typename...> class Template, typename... Args >
    struct is_instantiation_of< Template, Template<Args...> > : std::true_type {};

    template <typename Pointer>
    struct ptr_cmp
    {
        static_assert(std::is_pointer<Pointer>::value || is_instantiation_of<std::shared_ptr, Pointer>::value, "ptr_cmp must be used only with pointers and shared_ptrs");

        inline bool operator()(const Pointer & lptr, const Pointer & rptr) const
        {
            if (!rptr) return false; // nothing after expired pointer
            if (!lptr) return true;  // every not expired after expired pointer
            return *lptr < *rptr;
        }
    };

    template <typename WeakPointer>
    struct weak_ptr_cmp
    {
        static_assert(is_instantiation_of<std::weak_ptr, WeakPointer>::value, "weak_ptr_cmp must be used only with weak_ptrs");

        inline bool operator()(const WeakPointer & lhs, const WeakPointer & rhs) const
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
        static_assert(std::is_pointer<Pointer>::value || is_instantiation_of<std::shared_ptr, Pointer>::value, "ptr_hash must be used only with pointers and shared_ptrs");

        inline size_t operator()(const Pointer & ptr) const
        {
            if (!ptr) return std::hash<Pointer>()(ptr);

            return ptr->GetHash();
        }
    };

    template <typename WeakPointer>
    struct weak_ptr_hash
    {
        static_assert(is_instantiation_of<std::weak_ptr, WeakPointer>::value, "weak_ptr_hash must be used only with weak_ptrs");

        inline size_t operator()(const WeakPointer & wptr) const
        {
            auto ptr = wptr.lock();
            return ptr_hash<decltype(ptr)>()(ptr);
        }
    };

    template <typename Pointer>
    struct ptr_equal
    {
        static_assert(std::is_pointer<Pointer>::value || is_instantiation_of<std::shared_ptr, Pointer>::value, "ptr_hash must be used only with pointers and shared_ptrs");

        inline bool operator()(const Pointer & lhs, const Pointer & rhs) const
        {
            if (lhs == rhs) {
                return true;
            }

            if (!lhs || !rhs) {
                return false;
            }

            return *lhs == *rhs;
        }
    };

    template <typename WeakPointer>
    struct weak_ptr_equal
    {
        static_assert(is_instantiation_of<std::weak_ptr, WeakPointer>::value, "weak_ptr_hash must be used only with weak_ptrs");

        inline bool operator()(const WeakPointer & lhs, const WeakPointer & rhs) const
        {
            auto lptr = lhs.lock(), rptr = rhs.lock();
            return ptr_equal<decltype(lptr)>()(lptr, rptr);
        }
    };
}

template <typename K>
using TPSet = std::set<K, utils::ptr_cmp<K>>;

template <typename K, typename V>
using TPMap = std::map<K, V, utils::ptr_cmp<K>>;

template <typename K>
using TPUnorderedSet = std::unordered_set<K, utils::ptr_hash<K>, utils::ptr_equal<K>>;

template <typename K>
using TPWUnorderedSet = std::unordered_set<K, utils::weak_ptr_hash<K>, utils::weak_ptr_equal<K>>;

template <typename K, typename V>
using TPUnorderedMap = std::unordered_map<K, V, utils::ptr_hash<K>, utils::ptr_equal<K>>;

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

template <typename V, typename K, typename C>
std::map<K, V, C> MapFromSet(const std::set<K, C> & set)
{
    static_assert(std::is_default_constructible<V>::value, "value type must be default constructible");

    std::map<K, V, C> map;

    for (const auto & key: set) {
        map[key] = V();
    }

    return map;
}


inline uint8_t BitCountToRegCount(uint8_t bitCount, uint8_t width)
{
    return bitCount / width + bool(bitCount % width);
}

inline uint8_t BitCountToByteCount(uint8_t bitCount)
{
    return BitCountToRegCount(bitCount, 8);
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
        return Value = value;
    }

    operator T() const
    {
        return Value;
    }
};

template <typename Pointer>
struct TPSetView
{
    using TSet = TPSet<Pointer>;
    using Iterator = typename TSet::iterator;

    const Iterator Begin, End;
    const size_t Count;

    TPSetView(const Iterator & begin, const Iterator & end)
        : Begin(begin)
        , End(end)
        , Count(std::distance(begin, end))
    {}

    Pointer GetFirst() const
    {
        return *Begin;
    }

    Pointer GetLast() const
    {
        auto end = End;
        return *(--end);
    }
};
