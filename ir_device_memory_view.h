#pragma once

#include "declarations.h"

struct TIRDeviceMemoryView
{
    const uint8_t const *       Memory;
    const uint32_t              Size;
    const TMemoryBlockType &    Type;
    const uint32_t              StartAddress;
    const uint16_t              BlockSize;

    TIRDeviceMemoryView(uint8_t * memory, uint32_t size, const TMemoryBlockType & type, uint32_t startAddress, uint16_t blockSize);

    uint8_t GetByte(const PProtocolRegister & memoryBlock, uint16_t index) const;
};
