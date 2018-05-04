#include "ir_device_memory_view.h"
#include "memory_block.h"
#include "register_config.h"

#include <cassert>


using namespace std;

uint16_t TIRDeviceMemoryViewMetadata::GetBlockStart(const PMemoryBlock & memoryBlock) const
{
    assert(&memoryBlock->Type == &Type);
    assert(memoryBlock->Address >= StartAddress);

    auto byteIndex = (memoryBlock->Address - StartAddress) * BlockSize;

    assert(byteIndex < Size);

    return byteIndex;
}

uint16_t TIRDeviceMemoryViewMetadata::GetByteIndex(const PMemoryBlock & memoryBlock, uint16_t index) const
{
    assert(&memoryBlock->Type == &Type);
    assert(index < BlockSize);

    auto memoryBlockByteIndex = (Type.ByteOrder == EByteOrder::BigEndian) ? (BlockSize - index - 1) : index;

    auto byteIndex = GetBlockStart(memoryBlock) + memoryBlockByteIndex;

    assert(byteIndex < Size);

    return byteIndex;
}
