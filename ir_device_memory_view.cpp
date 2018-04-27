#include "ir_device_memory_view.h"
#include "protocol_register.h"
#include "register_config.h"
#include "protocol_register_bind_info.h"
#include "virtual_register.h"

#include <cassert>
#include <iostream>
#include <bitset>

using namespace std;

namespace
{
    template <typename T = uint64_t>
    inline T MersenneNumber(uint8_t bitCount)
    {
        static_assert(is_unsigned<T>::value, "mersenne number may be only unsigned");
        assert(bitCount <= sizeof(T));
        return (T(1) << bitCount) - 1;
    }
}


// TIRDeviceMemoryView::TIRDeviceMemoryView(const uint8_t * memory, uint32_t size, const TMemoryBlockType & type, uint32_t startAddress, uint16_t blockSize)
//     : RawMemory(memory)
//     , Size(size)
//     , Type(type)
//     , StartAddress(startAddress)
//     , BlockSize(blockSize)
// {}

uint64_t TIRDeviceMemoryView::Get(const TIRDeviceValueDesc & valueDesc) const
{
    uint64_t value = 0;

    uint8_t bitPosition = 0;

    auto readMemoryBlock = [&](const pair<const PProtocolRegister, TProtocolRegisterBindInfo> & boundMemoryBlock){
        const auto & memoryBlock = boundMemoryBlock.first;
        const auto & bindInfo = boundMemoryBlock.second;

        const auto mask = MersenneNumber(bindInfo.BitCount()) << bindInfo.BitStart;

        for (uint16_t iByte = BitCountToByteCount(bindInfo.BitStart) - 1; iByte < memoryBlock->Size; ++iByte) {
            uint16_t begin = max(uint16_t(iByte * 8), bindInfo.BitStart);
            uint16_t end = min(uint16_t((iByte + 1) * 8), bindInfo.BitEnd);

            if (begin >= end)
                continue;

            uint64_t bits = GetByte(memoryBlock, iByte) & (mask >> (iByte * 8));
            value |= bits << bitPosition;

            auto bitCount = end - begin;
            bitPosition += bitCount;
        }

        // TODO: check for usefullness
        if (Global::Debug) {
            cerr << "reg mask: " << bitset<64>(mask) << endl;
            cerr << "reading " << bindInfo.Describe() << " bits of " << memoryBlock->Describe()
                 << " to [" << (int)bitPosition << ", " << int(bitPosition + bindInfo.BitCount() - 1) << "] bits of value" << endl;
        }
    };

    if (valueDesc.WordOrder == EWordOrder::BigEndian) {
        for_each(valueDesc.BoundMemoryBlocks.rbegin(), valueDesc.BoundMemoryBlocks.rend(), readMemoryBlock);
    } else {
        for_each(valueDesc.BoundMemoryBlocks.begin(), valueDesc.BoundMemoryBlocks.end(), readMemoryBlock);
    }

    if (Global::Debug)
        cerr << "map value from registers: " << value << endl;

    return value;
}

void TIRDeviceMemoryView::Set(const TIRDeviceValueDesc & valueDesc, uint64_t value)
{
    if (Global::Debug)
        cerr << "map value to registers: " << value << endl;

    uint8_t bitPosition = 0;

    auto readMemoryBlock = [&](const pair<const PProtocolRegister, TProtocolRegisterBindInfo> & protocolRegisterBindInfo){
        const auto & memoryBlock = protocolRegisterBindInfo.first;
        const auto & bindInfo = protocolRegisterBindInfo.second;

        const auto mask = MersenneNumber(bindInfo.BitCount()) << bindInfo.BitStart;

        for (uint16_t iByte = BitCountToByteCount(bindInfo.BitStart) - 1; iByte < memoryBlock->Size; ++iByte) {
            uint16_t begin = max(uint16_t(iByte * 8), bindInfo.BitStart);
            uint16_t end = min(uint16_t((iByte + 1) * 8), bindInfo.BitEnd);

            if (begin >= end)
                continue;

            auto byteMask = mask >> (iByte * 8);

            auto cachedBits = memoryBlock->GetCachedByte(iByte);

            GetByte(memoryBlock, iByte) = (~byteMask & cachedBits) | (byteMask & (value >> bitPosition));

            auto bitCount = end - begin;
            bitPosition += bitCount;
        }
    };

    if (valueDesc.WordOrder == EWordOrder::BigEndian) {
        for_each(valueDesc.BoundMemoryBlocks.rbegin(), valueDesc.BoundMemoryBlocks.rend(), readMemoryBlock);
    } else {
        for_each(valueDesc.BoundMemoryBlocks.begin(), valueDesc.BoundMemoryBlocks.end(), readMemoryBlock);
    }

    // for (auto protocolRegisterBindInfo = registerMap.rbegin(); protocolRegisterBindInfo != registerMap.rend(); ++protocolRegisterBindInfo) {
    //     const auto & protocolRegister = protocolRegisterBindInfo->first;
    //     const auto & bindInfo = protocolRegisterBindInfo->second;

    //     auto mask = MersenneNumber(bindInfo.BitCount()) << bindInfo.BitStart;

    //     const auto & cachedRegisterValue = protocolRegister->GetValue();

    //     auto registerValue = (~mask & cachedRegisterValue) | (mask & ((value >> bitPosition) << bindInfo.BitStart));

    //     if (Global::Debug) {
    //         cerr << "cached reg val: " << cachedRegisterValue << " (0x" << hex << cachedRegisterValue << dec << ")" << endl;
    //         cerr << "reg mask: " << bitset<64>(mask) << endl;
    //         cerr << "reg value: " << registerValue << " (0x" << hex << registerValue << dec << ")" << endl;
    //         cerr << "writing [" << (int)bitPosition << ", " << int(bitPosition + bindInfo.BitCount() - 1) << "]" << " bits of value "
    //              << " to " << bindInfo.Describe() << " bits of " << protocolRegister->Describe() << endl;
    //     }

    //     query->SetValue(protocolRegister, registerValue);

    //     bitPosition += bindInfo.BitCount();
    // }
}

const uint8_t * TIRDeviceMemoryView::GetMemoryBlockData(const PProtocolRegister & memoryBlock) const
{
    return RawMemory + GetBlockStart(memoryBlock);
}

const uint8_t & TIRDeviceMemoryView::GetByte(const PProtocolRegister & memoryBlock, uint16_t index) const
{
    assert(&memoryBlock->Type == &Type);
    assert(index < BlockSize);

    auto memoryBlockByteIndex = (Type.ByteOrder == EByteOrder::BigEndian) ? index : (BlockSize - index - 1);

    auto byteIndex = GetBlockStart(memoryBlock) + memoryBlockByteIndex;

    assert(byteIndex < Size);

    return RawMemory[byteIndex];
}

uint16_t TIRDeviceMemoryView::GetBlockStart(const PProtocolRegister & memoryBlock) const
{
    assert(&memoryBlock->Type == &Type);
    assert(memoryBlock->Address >= StartAddress);

    auto byteIndex = (memoryBlock->Address - StartAddress) * BlockSize;

    assert(byteIndex < Size);

    return byteIndex;
}
