#pragma once

#include "serial_config.h"
#include "declarations.h"


struct TDeviceSetupItem : public TDeviceSetupItemConfig
{
    PVirtualRegister Register;

    TDeviceSetupItem(PSerialDevice device, PDeviceSetupItemConfig config);

    bool Write() const;
    std::string ToString() const;
    std::string Describe() const;
    PSerialDevice GetDevice() const;
};
