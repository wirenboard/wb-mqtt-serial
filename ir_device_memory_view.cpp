#include "ir_device_memory_view.h"
#include "memory_block.h"
#include "register_config.h"

#include <cassert>


using namespace std;

uint16_t TIRDeviceMemoryBlockViewMetadata::GetByteIndex(uint16_t index) const
{
    assert(index < MemoryBlock->Size);

    auto byteIndex = (MemoryBlock->Type.ByteOrder == EByteOrder::BigEndian) ? (MemoryBlock->Size - index - 1) : index;

    assert(byteIndex < MemoryBlock->Size);

    return byteIndex;
}

uint16_t TIRDeviceMemoryViewMetadata::GetBlockStart(const CPMemoryBlock & memoryBlock) const
{
    assert(&memoryBlock->Type == &Type);
    assert(memoryBlock->Address >= StartAddress);

    auto byteIndex = (memoryBlock->Address - StartAddress) * BlockSize;

    assert(byteIndex < Size);

    return byteIndex;
}
