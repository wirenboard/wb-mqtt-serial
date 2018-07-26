#include "ir_bind_info.h"
#include "memory_block.h"

#include <sstream>
#include <cassert>

using namespace std;


namespace
{
    template <typename T = uint64_t>
    inline T MersenneNumber(uint8_t bitCount)
    {
        static_assert(is_unsigned<T>::value, "mersenne number may be only unsigned");
        assert(bitCount <= sizeof(T) * 8);
        return (T(1) << bitCount) - 1;
    }
}

TIRBindInfo::TIRBindInfo(uint16_t start, uint16_t end)
    : BitStart(start)
    , BitEnd(end)
{
    assert(BitStart < BitEnd);
}

uint64_t TIRBindInfo::GetMask() const
{
    return MersenneNumber(BitCount());
}

string TIRBindInfo::Describe() const
{
    ostringstream ss;
    ss << "[" << BitStart << ", " << BitEnd - 1 << "]";
    return ss.str();
}

bool TIRDeviceValueDesc::operator<(const TIRDeviceValueDesc & rhs) const
{
    for (const auto & boundMemoryBlock: rhs.MemoryBlocks) {
        const auto & memoryBlock = boundMemoryBlock.first;
        const auto & otherBindInfo = boundMemoryBlock.second;

        const auto & itBindInfo = MemoryBlocks.find(memoryBlock);
        if (itBindInfo != MemoryBlocks.end()) {
            const auto & bindInfo = itBindInfo->second;
            if (bindInfo.BitStart >= otherBindInfo.BitEnd) {
                return false;
            } else if(otherBindInfo.BitStart >= bindInfo.BitEnd) {
                return true;
            } else {
                return false;   // collision
            }
        }
    }

    const auto & lhsFirst = *MemoryBlocks.begin()->first;
    const auto & rhsFirst = *rhs.MemoryBlocks.begin()->first;

    return lhsFirst < rhsFirst;
}
