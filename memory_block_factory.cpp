#include "memory_block_factory.h"
#include "memory_block.h"
#include "register_config.h"
#include "serial_device.h"

#include <cassert>

using namespace std;

TPMap<PMemoryBlock, TIRBindInfo> TMemoryBlockFactory::GenerateMemoryBlocks(
    const PRegisterConfig & config, const PSerialDevice & device)
{
    TPMap<PMemoryBlock, TIRBindInfo> memoryBlocksBindInfo;

    uint16_t memoryBlockSize;
    try {
        const auto & type = device->Protocol()->GetRegType(config->Type);
        memoryBlockSize = type.IsVariadicSize() ? RegisterFormatMaxSize(config->Format) : type.Size;
    } catch (out_of_range &) {
        throw TSerialDeviceException(
            "unknown type name: '" + config->TypeName +
            "' of " + device->ToString() + ": " + config->ToString()
        );
    }

    auto bitsToAllocate = config->GetWidth();
    const auto memoryBlockWidth = uint16_t(memoryBlockSize * 8);

    // we limit our shifting range with memory block width
    // or with format width because if you need to shift
    // on memory block, you should specify higher
    // memory block address instead.
    if (memoryBlockWidth <= config->BitOffset) {
        throw TSerialDeviceException(
            "unable to create memoryBlocks by config " + config->ToString() +
            ": specified bit shift (" + to_string((int)config->BitOffset) +
            ") must be less than device memory block width (" +
                to_string(memoryBlockWidth) + ")"
        );
    }

    // find out how many memory blocks are needed to represent
    // configured register. As we restricted bit offset to be
    // less than memory block width, we don't need to worry
    // about getting redundant blocks after adding offset.
    auto mbCount = BitCountToRegCount(
        uint16_t(bitsToAllocate + config->BitOffset), memoryBlockWidth
    );

    if (Global::Debug) {
        cerr << "bits: " << (int)bitsToAllocate << endl;
        cerr << "split " << config->ToString() << " to " << mbCount
             << " " << config->TypeName << " memory blocks" << endl;
    }

    for (auto mbLEIndex = 0; mbLEIndex < mbCount; ++mbLEIndex) {
        const auto mbBEIndex = mbCount - mbLEIndex - 1;
        const auto mbIndex   = config->WordOrder == EWordOrder::BigEndian ? mbBEIndex : mbLEIndex;
        const auto type      = config->Type;
        const auto address   = config->Address + mbIndex;

        uint16_t startBit = max(int(config->BitOffset) - int(mbLEIndex * memoryBlockWidth), 0);
        uint16_t endBit   = min(memoryBlockWidth, uint16_t(startBit + bitsToAllocate));

        auto memoryBlock = device->GetCreateMemoryBlock(address, type, memoryBlockSize);

        const auto & insertResult = memoryBlocksBindInfo.insert({
            memoryBlock, { startBit, endBit }
        });
        const auto & bindInfo = insertResult.first->second;
        bitsToAllocate -= bindInfo.BitCount();

        // insertion occured - no duplicates
        assert(insertResult.second);
        // at end of loop all bit should be allocated
        assert(mbLEIndex != mbCount - 1 || bitsToAllocate == 0);
    }

    return move(memoryBlocksBindInfo);
}
