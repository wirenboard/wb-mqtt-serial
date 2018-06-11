#pragma once

#include "utils.h"
#include "declarations.h"
#include "ir_bind_info.h"

class TMemoryBlockFactory
{
    TMemoryBlockFactory() = delete;
public:
    static TPMap<PMemoryBlock, TIRBindInfo> GenerateMemoryBlocks(const PRegisterConfig & config, const PSerialDevice & device);
};
