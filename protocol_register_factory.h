#pragma once

#include "utils.h"
#include "declarations.h"
#include "protocol_register_bind_info.h"

class TMemoryBlockFactory
{
    TMemoryBlockFactory() = delete;
public:
    static TPMap<PMemoryBlock, TMemoryBlockBindInfo> GenerateMemoryBlocks(const PRegisterConfig & config, const PSerialDevice & device);
};
