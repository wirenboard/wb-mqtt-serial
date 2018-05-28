#pragma once

#include "declarations.h"

#include <cassert>

/**
 * @brief: Provides value - level access to memory
 *  Instead of accessing individual bytes, it allows
 *  to read and write values in memory block.
 *
 * @note: it is not meant to give final values for publishing,
 *  but some intermediate separate raw values held by memory block
 *  (ex. mercury230's array memory block may be represented as
 *   4 TIRDeviceMemoryBlockValue objects, each returning U32 value)
 *  It's use is limited for now to few cases because virtual registers
 *  that produce final values are associated with memory blocks and
 *  not with values, that are stored inside them.
 */
struct TIRDeviceMemoryBlockValue
{
    const TIRDeviceMemoryBlockView & View;
    const uint16_t                   StartByteIndex,
                                     EndByteIndex;

    TIRDeviceMemoryBlockValue(const TIRDeviceMemoryBlockValue &) = delete;
    TIRDeviceMemoryBlockValue(TIRDeviceMemoryBlockValue &&) = delete;

    template <typename T>
    operator T() const
    {
        CheckType<T>();

        T value = 0;
        // yeah it's reinterpret cast, but GetValueImpl is aware of platform endiannes
        GetValueImpl(reinterpret_cast<uint8_t *>(&value), sizeof(T));
        return value;
    }

    template <typename T>
    TIRDeviceMemoryBlockValue & operator=(const T & value)
    {
        CheckType<T>();

        // yeah it's reinterpret cast, but SetValueImpl is aware of platform endiannes
        SetValueImpl(reinterpret_cast<const uint8_t *>(&value), sizeof(T));
        return *this;
    }

private:
    template <typename T>
    void CheckType() const
    {
        static_assert(std::is_pod<T>::value, "only POD types");
        assert("any non integral type (float or user type) is not safe to access partially - it should be exact size" &&
               (std::is_integral<T>::value || (EndByteIndex - StartByteIndex) == sizeof(T)));
    }

    /**
     *  @brief: platform endiannes - aware raw memory access interface
     */
    void GetValueImpl(uint8_t * pValue, uint8_t size) const;
    void SetValueImpl(const uint8_t * pValue, uint8_t size);
};

template <typename T>
inline bool operator==(const T & lhs, const TIRDeviceMemoryBlockValue & rhs)
{
    return lhs == static_cast<T>(rhs);
}

template <typename T>
inline bool operator==(const TIRDeviceMemoryBlockValue & lhs, const T & rhs)
{
    return static_cast<T>(lhs) == rhs;
}

/**
 * @brief: If we need to modify memory pointed by View but not view itself,
 *  we should express that in code by dereferencing View, which will give
 *  object of this class, operations on which will result in modification
 *  of values stored in memory
 */
struct TIRDeviceMemoryBlockMemory
{
    const TIRDeviceMemoryBlockView & View;

    TIRDeviceMemoryBlockMemory(const TIRDeviceMemoryBlockMemory &) = delete;
    TIRDeviceMemoryBlockMemory(TIRDeviceMemoryBlockMemory &&) = delete;

    TIRDeviceMemoryBlockMemory & operator=(const TIRDeviceMemoryBlockMemory &);
};

/**
 *
 *
 */
struct TIRDeviceMemoryBlockView
{
    uint8_t * const     RawMemory;
    const CPMemoryBlock MemoryBlock;
    const bool          Readonly;

    inline operator bool() const
    {
        return bool(RawMemory);
    }

    inline bool operator==(const TIRDeviceMemoryBlockView & other) const
    {
        return RawMemory == other.RawMemory;
    }

    inline bool operator!=(const TIRDeviceMemoryBlockView & other) const
    {
        return !((*this) == other);
    }

    inline TIRDeviceMemoryBlockMemory operator*() const
    {
        return { *this };
    }

    inline TIRDeviceMemoryBlockValue operator[](const uint16_t index) const
    {
        uint16_t start = GetValueByteIndex(index);
        uint16_t end = start + GetValueSize(index);

        return { *this, start, end };
    }

    uint16_t GetByteIndex(uint16_t index) const;
    uint16_t GetValueByteIndex(uint16_t index) const;
    uint16_t GetValueSize(uint16_t index) const;

    uint8_t GetByte(uint16_t index) const;
    void SetByte(uint16_t index, uint8_t) const;
};

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

    uint64_t ReadValue(const TIRDeviceValueDesc &) const;
    void WriteValue(const TIRDeviceValueDesc &, uint64_t value) const;

    static void WriteValue(const TIRDeviceValueDesc &, uint64_t value, TMemoryBlockViewProvider);
};
