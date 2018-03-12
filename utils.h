#pragma once

#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <type_traits>
#include <sstream>

namespace utils
{
    template < template <typename...> class Template, typename T >
    struct is_instantiation_of : std::false_type {};

    template < template <typename...> class Template, typename... Args >
    struct is_instantiation_of< Template, Template<Args...> > : std::true_type {};

    template < typename T >
    struct is_pointer : std::integral_constant<bool, std::is_pointer<T>::value || is_instantiation_of<std::shared_ptr, T>::value> {};

    template < typename T >
    struct is_weak_pointer : is_instantiation_of<std::weak_ptr, T> {};

    template <typename Pointer>
    struct ptr_cmp
    {
        static_assert(is_pointer<Pointer>::value, "ptr_cmp must be used only with pointers and shared_ptrs");

        static bool cmp(const Pointer & lptr, const Pointer & rptr)
        {
            if (!rptr) return false; // nothing after expired pointer
            if (!lptr) return true;  // every not expired after expired pointer
            return *lptr < *rptr;
        }

        inline bool operator()(const Pointer & lptr, const Pointer & rptr) const
        {
            return cmp(lptr, rptr);
        }
    };

    template <typename WeakPointer>
    struct weak_ptr_cmp
    {
        static_assert(is_weak_pointer<WeakPointer>::value, "weak_ptr_cmp must be used only with weak_ptrs");

        inline bool operator()(const WeakPointer & lhs, const WeakPointer & rhs) const
        {
            auto lptr = lhs.lock(), rptr = rhs.lock();
            return ptr_cmp<decltype(lptr)>::cmp(lptr, rptr);
        }
    };

    template <typename Pointer>
    struct ptr_hash
    {
        static_assert(is_pointer<Pointer>::value, "ptr_hash must be used only with pointers and shared_ptrs");

        inline size_t operator()(const Pointer & ptr) const
        {
            if (!ptr) return std::hash<Pointer>()(ptr);

            return ptr->GetHash();
        }
    };

    template <typename WeakPointer>
    struct weak_ptr_hash
    {
        static_assert(is_weak_pointer<WeakPointer>::value, "weak_ptr_hash must be used only with weak_ptrs");

        inline size_t operator()(const WeakPointer & wptr) const
        {
            auto ptr = wptr.lock();
            return ptr_hash<decltype(ptr)>()(ptr);
        }
    };

    template <typename Pointer>
    struct ptr_equal
    {
        static_assert(is_pointer<Pointer>::value, "ptr_hash must be used only with pointers and shared_ptrs");

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
        static_assert(is_weak_pointer<WeakPointer>::value, "weak_ptr_hash must be used only with weak_ptrs");

        inline bool operator()(const WeakPointer & lhs, const WeakPointer & rhs) const
        {
            auto lptr = lhs.lock(), rptr = rhs.lock();
            return ptr_equal<decltype(lptr)>()(lptr, rptr);
        }
    };

    template<typename T, typename ... Args>
    std::unique_ptr<T> make_unique(Args && ... args)
    {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }
}

template <typename K>
using TPSet = std::set<K, utils::ptr_cmp<K>>;

template <typename K, typename V>
using TPMap = std::map<K, V, utils::ptr_cmp<K>>;

template <typename K>
using TPWSet = std::set<K, utils::weak_ptr_cmp<K>>;

template <typename K>
using TPUnorderedSet = std::unordered_set<K, utils::ptr_hash<K>, utils::ptr_equal<K>>;

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

template<typename Pointer>
inline bool IsSubset(const TPSet<Pointer> & set, const TPSet<Pointer> & subset)
{
    return includes(set.begin(), set.end(), subset.begin(), subset.end(), utils::ptr_cmp<Pointer>::cmp);
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

    const Iterator First, Last;
    const size_t Count;

    TPSetView(const Iterator & first, const Iterator & last)
        : First(first)
        , Last(last)
        , Count(std::distance(first, last) + 1)
    {}

    Pointer GetFirst() const
    {
        return *First;
    }

    Pointer GetLast() const
    {
        return *Last;
    }

    Iterator Begin() const
    {
        return First;
    }

    Iterator End() const
    {
        auto end = Last;
        return ++end;
    }
};

template<typename Iterator>
std::string PrintRange(
    Iterator pos,
    const Iterator end,
    std::function<void(std::ostream&, typename Iterator::value_type)> && toStream = [](std::ostream & s, typename Iterator::value_type val){ s << val; },
    bool multiline = false,
    const char * delimiter = ", "
)
{
    std::ostringstream ss;

    ss << "[";

    if (multiline) {
        ss << std::endl;
    }

    while (pos != end) {
        toStream(ss, *pos);

        if (++pos != end) {
            ss << delimiter;
        }

        if (multiline) {
            ss << std::endl;
        }
    }

    ss << "]";

    return ss.str();
}

template<typename Collection>
std::string PrintCollection(
    const Collection & c,
    std::function<void(std::ostream&, typename Collection::value_type)> && toStream = [](std::ostream & s, typename Collection::value_type val){ s << val; },
    bool multiline = false,
    const char * delimiter = ", "
)
{
    return PrintRange(c.begin(), c.end(), std::move(toStream), multiline, delimiter);
}
