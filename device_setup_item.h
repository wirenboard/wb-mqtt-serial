#pragma once

#include "serial_config.h"
#include "ir_bind_info.h"


struct TDeviceSetupItem : public TDeviceSetupItemConfig
{
    TBoundMemoryBlocks  BoundMemoryBlocks;
    PIRDeviceValueQuery Query;
    PIRValue            ManagedValue;

    TDeviceSetupItem(PSerialDevice device, PDeviceSetupItemConfig config);
    ~TDeviceSetupItem();

    std::string ToString() const;
    std::string Describe() const;
    PSerialDevice GetDevice() const;
};
