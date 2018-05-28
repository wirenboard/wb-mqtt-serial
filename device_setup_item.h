#pragma once

#include "serial_config.h"
#include "memory_block_bind_info.h"


struct TDeviceSetupItem : public TDeviceSetupItemConfig
{
    TBoundMemoryBlocks  BoundMemoryBlocks;
    PIRDeviceValueQuery Query;

    TDeviceSetupItem(PSerialDevice device, PDeviceSetupItemConfig config);

    std::string ToString() const;
    std::string Describe() const;
    PSerialDevice GetDevice() const;
};
