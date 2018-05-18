#include "memory_block_factory.h"
#include "memory_block.h"
#include "register_config.h"
#include "serial_device.h"

#include <cassert>

using namespace std;

TPMap<PMemoryBlock, TMemoryBlockBindInfo> TMemoryBlockFactory::GenerateMemoryBlocks(const PRegisterConfig & config, const PSerialDevice & device)
{
    TPMap<PMemoryBlock, TMemoryBlockBindInfo> memoryBlocksBindInfo;

    uint16_t memoryBlockSize;
    try {
        const auto & type = device->Protocol()->GetRegType(config->Type);
        memoryBlockSize = type.IsVariadicSize() ? RegisterFormatByteWidth(config->Format) : type.Size;
    } catch (out_of_range &) {
        throw TSerialDeviceException("unknown type name: '" + config->TypeName + "' of " + device->ToString() + ": " + config->ToString());
    }

    const uint16_t memoryBlockBitWidth = memoryBlockSize * 8;
    const auto formatBitWidth = config->GetFormatBitWidth();
    auto bitsToAllocate = config->GetBitWidth();

    if (formatBitWidth <= config->BitOffset) {
        throw TSerialDeviceException("unable to create memoryBlocks by config " + config->ToString() +
                                     ": specified bit shift (" + to_string((int)config->BitOffset) +
                                     ") must be less than specified format's (" + RegisterFormatName(config->Format) + ") bit width");
    }

    if (bitsToAllocate > (formatBitWidth - config->BitOffset)) {
        throw TSerialDeviceException("unable to create memoryBlocks by config " + config->ToString() +
                                     ": specified bit width (" + to_string((int)memoryBlockBitWidth) +
                                     ") and shift (" + to_string((int)config->BitOffset) +
                                     ") does not fit into specified format (" + RegisterFormatName(config->Format) + ")");
    }

    uint16_t mbIndexStart = config->BitOffset / memoryBlockBitWidth;
    uint16_t mbIndexEnd   = BitCountToRegCount(uint16_t(formatBitWidth), memoryBlockBitWidth);

    if (Global::Debug) {
        cerr << "bits: " << (int)bitsToAllocate << endl;
        cerr << "split " << config->ToString() << " to " << (mbIndexEnd - mbIndexStart) << " " << config->TypeName << " memoryBlocks" << endl;
    }

    for (auto mbIndex = mbIndexStart; mbIndex < mbIndexEnd; ++mbIndex) {
        const auto mbReverseIndex = mbIndexEnd - mbIndex - 1;
        const auto type            = config->Type;
        const auto address         = config->Address + (config->WordOrder == EWordOrder::BigEndian ? mbReverseIndex : mbIndex);

        uint16_t startBit = max(int(config->BitOffset) - int(mbIndex * memoryBlockBitWidth), 0);
        uint16_t endBit   = min(memoryBlockBitWidth, uint16_t(startBit + bitsToAllocate));

        auto memoryBlock = device->GetCreateMemoryBlock(address, type, memoryBlockSize);

        const auto & insertResult = memoryBlocksBindInfo.insert({ memoryBlock, { startBit, endBit } });
        const auto & bindInfo = insertResult.first->second;
        bitsToAllocate -= bindInfo.BitCount();

        assert(insertResult.second); // emplace occured - no duplicates
        assert(mbIndex != mbIndexEnd || bitsToAllocate == 0); // at end of loop all bit should be allocated

        if (!bitsToAllocate) {
            break;
        }
    }

    return move(memoryBlocksBindInfo);
}
