#pragma once

#include "declarations.h"

#include <cassert>

/**
 * @brief template that adds conversion
 *  and assignment operators to seemlessly
 *  interoperate with C++ data types.
 *
 * @note Helds no logic. Basically a syntactic
 *  sugar.
 */
template <class View>
struct TIRImplicitValue: public View
{
    template <typename ... Args>
    TIRImplicitValue(Args && ... args)
        : View(std::forward<Args>(args)...)
    {}

    template <typename T>
    operator T() const
    {
        View::template CheckType<T>();

        T value {};
        // yeah it's reinterpret cast, but GetValueImpl is aware of platform endiannes
        View::GetValueImpl(reinterpret_cast<uint8_t *>(&value));
        return value;
    }

    template <typename T>
    TIRImplicitValue & operator=(const T & value)
    {
        View::template CheckType<T>();

        // yeah it's reinterpret cast, but SetValueImpl is aware of platform endiannes
        View::SetValueImpl(reinterpret_cast<const uint8_t *>(&value));
        return *this;
    }
};

struct TIRDeviceMemoryBlockViewImpl;
using TIRDeviceMemoryBlockView = TIRImplicitValue<TIRDeviceMemoryBlockViewImpl>;

/**
 * @brief Provides value - level access to memory
 *  Instead of accessing individual bytes, it allows
 *  to read and write values in memory block.
 *
 * @note it is not meant to give final values for publishing,
 *  but some intermediate separate raw values held by memory block
 *  (ex. mercury230's array memory block may be represented as
 *   4 TIRDeviceValueView objects, each returning U32 value)
 *  It's use is limited for now to few cases because virtual registers
 *  that produce final values are associated with memory blocks and
 *  not with values, that are stored inside them.
 */
struct TIRDeviceValueViewImpl
{
    friend TIRDeviceMemoryBlockViewImpl;

    uint8_t * const RawMemory;
    const uint8_t   Size;
    const bool      Readonly : 1;
    const bool      IsLE : 1;

    TIRDeviceValueViewImpl(uint8_t * raw, uint8_t size, bool readonly, bool isLE);
    TIRDeviceValueViewImpl(const TIRDeviceValueViewImpl &) = delete;
    TIRDeviceValueViewImpl(TIRDeviceValueViewImpl &&) = delete;

protected:
    template <typename T>
    void CheckType() const
    {
        static_assert(std::is_fundamental<T>::value, "only fundamental types");
        // any non integral type (looking at you, float) is not safe to access partially - it should be exact size
        assert("size mismatch on non-integral type" &&
               (std::is_integral<T>::value || Size == sizeof(T)));
        assert(sizeof(T) >= Size); // make sure all memory is used
    }

    /**
     *  @brief platform endiannes - aware raw memory access interface
     */
    void GetValueImpl(uint8_t * pValue) const;
    void SetValueImpl(const uint8_t * pValue) const;

private:
    /**
     *  @brief inverts byte index if platform is big-endian
     */
    uint8_t PlatformEndiannesAware(uint8_t iByte) const;
    /**
     *  @brief inverts byte index if memory block is big-endian
     */
    uint8_t MemoryBlockEndiannesAware(uint8_t iByte) const;

    uint8_t GetByte(uint8_t index) const;
    void SetByte(uint8_t index, uint8_t) const;
};

using TIRDeviceValueView = TIRImplicitValue<TIRDeviceValueViewImpl>;

/**
 * @brief If we need to modify memory pointed by View but not view itself,
 *  we should express that in code by dereferencing View, which will give
 *  object of this class, operations on which will result in modification
 *  of values stored in memory
 *
 * @note You may think about this class as some sort of
 *  behaviour modifier. This in particular, modifies
 *  behaviour of an assignment operator.
 */
struct TIRDeviceMemoryBlockMemory
{
    const TIRDeviceMemoryBlockViewImpl & View;

    /* explicit constructor for stupid g++4.7 */
    TIRDeviceMemoryBlockMemory(const TIRDeviceMemoryBlockViewImpl & view)
        : View(view)
    {}
    TIRDeviceMemoryBlockMemory(const TIRDeviceMemoryBlockMemory &) = delete;
    TIRDeviceMemoryBlockMemory(TIRDeviceMemoryBlockMemory &&) = delete;

    TIRDeviceMemoryBlockMemory & operator=(const TIRDeviceMemoryBlockMemory &);
};

/**
 * @brief Implements managed access to block's memory.
 *  In memory block one or more values may be stored.
 *  Value is meaningful chunk of memory that can be
 *  represented as some object of fundamental type (int, float, etc.).
 *  Separate values of memory block are accessed via index operator
 *  according to layout that was specified at protocol's daclaration.
 *
 * @note Memory block view can convert entire block memory into
 *  any POD type of same as block size, thus allowing to
 *  convert raw memory even to user structs.
 *  When convering entire memory block,
 *  memory block's layout is also taken into account.
 */
struct TIRDeviceMemoryBlockViewImpl
{
    uint8_t * const     RawMemory;
    const CPMemoryBlock MemoryBlock;
    const bool          Readonly;

    /* redundant constructor for stupid g++4.7 */
    TIRDeviceMemoryBlockViewImpl(uint8_t * rawMemory, const CPMemoryBlock & mb, bool readonly)
        : RawMemory(rawMemory)
        , MemoryBlock(mb)
        , Readonly(readonly)
    {}
    TIRDeviceMemoryBlockViewImpl(const TIRDeviceMemoryBlockViewImpl &) = default;

    inline operator bool() const
    {
        return bool(RawMemory);
    }

    inline bool operator==(const TIRDeviceMemoryBlockViewImpl & other) const
    {
        return RawMemory == other.RawMemory;
    }

    inline bool operator!=(const TIRDeviceMemoryBlockViewImpl & other) const
    {
        return !((*this) == other);
    }

    inline TIRDeviceMemoryBlockMemory operator*() const
    {
        return { *this };
    }

    /**
     * @brief takes value index and returns value view
     */
    TIRDeviceValueView operator[](uint16_t index) const;

    /**
     * @brief takes little-endian byte index
     *  and returns real one based on memory
     *  block byte order.
     *  By other words: inverts index if
     *  memory block is big-endian.
     */
    uint16_t GetByteIndex(uint16_t index) const;

    /**
     * @brief takes value index and returns
     *  its size in bytes. Max size is 8 thus
     *  64-bit value is largest possible.
     */
    uint8_t  GetValueSize(uint16_t index) const;
    uint16_t GetSize() const;

    uint8_t GetByte(uint16_t index) const;
    void SetByte(uint16_t index, uint8_t) const;

protected:
    template <typename T>
    void CheckType() const
    {
        static_assert(std::is_pod<T>::value, "only POD types");
        // any non integral type (float or user type) is not safe to access partially - it should be exact size
        assert("size mismatch on non-integral type" &&
               (std::is_integral<T>::value || GetSize() == sizeof(T)));
        assert("not all memory of block is used" && sizeof(T) >= GetSize());
    }

    void CheckByteIndex(uint16_t) const;

    /**
     * @brief platform endiannes - aware raw memory access interface
     *
     * @note we don't need size here as we rely on CheckType to ensure
     *  that too small types won't make it through.
     */
    void GetValueImpl(uint8_t * pValue) const;
    void SetValueImpl(const uint8_t * pValue) const;
};

/**
 * @brief References (not owns) memory of one or
 *  more memory blocks. Allows to access memory of
 *  individual memory blocks. Used for device request
 *  and response data section interpretation.
 */
struct TIRDeviceMemoryView
{
    uint8_t * const             RawMemory;
    const uint32_t              Size;
    const TMemoryBlockType &    Type;
    const uint32_t              StartAddress;
    const uint16_t              BlockSize;
    const bool                  Readonly;

    TIRDeviceMemoryView(uint8_t * memory, uint32_t size, const TMemoryBlockType &, uint32_t start, uint16_t blockSize, bool readonly = false);
    TIRDeviceMemoryView(const uint8_t * memory, uint32_t size, const TMemoryBlockType &, uint32_t start, uint16_t blockSize);

    void Clear() const;

    TIRDeviceMemoryBlockView operator[](const CPMemoryBlock & memoryBlock) const;

    using TMemoryBlockViewProvider = std::function<TIRDeviceMemoryBlockView(const CPMemoryBlock &)>;

    void ReadValue(const TIRDeviceValueContext &) const;
    void WriteValue(const TIRDeviceValueContext &) const;

    static void WriteValue(const TIRDeviceValueContext &, TMemoryBlockViewProvider);
};
