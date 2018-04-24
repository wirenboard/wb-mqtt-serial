#include "ir_device_memory_view.h"
#include "protocol_register.h"
#include "register_config.h"

#include <cassert>

TIRDeviceMemoryView::TIRDeviceMemoryView(uint8_t * memory, uint32_t size, const TMemoryBlockType & type, uint32_t startAddress, uint16_t blockSize)
    : Memory(memory)
    , Size(size)
    , Type(type)
    , StartAddress(startAddress)
    , BlockSize(blockSize)
{}

uint8_t TIRDeviceMemoryView::GetByte(const PProtocolRegister & memoryBlock, uint16_t index) const
{
    assert(&memoryBlock->Type == &Type);
    assert(index < BlockSize);

    auto memoryBlockIndex = memoryBlock->Address - StartAddress;
    auto memoryBlockByteIndex = (Type.ByteOrder == EByteOrder::BigEndian) ? index : (BlockSize - index - 1);

    auto byteIndex = memoryBlockIndex * BlockSize + memoryBlockByteIndex;

    assert(byteIndex < Size);

    return Memory[byteIndex];
}
