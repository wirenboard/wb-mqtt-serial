#pragma once

#include "serial_config.h"
#include "ir_bind_info.h"
#include "virtual_value.h"


struct TDeviceSetupItem final: IVirtualValue,
                               TDeviceSetupItemConfig,
                               std::enable_shared_from_this<TDeviceSetupItem>
{
    TIRDeviceValueDesc  ValueDesc;
    PIRDeviceValueQuery Query;
    PIRValue            ManagedValue;

    static PDeviceSetupItem Create(PSerialDevice device, PDeviceSetupItemConfig config);
    ~TDeviceSetupItem();

    TIRDeviceValueContext GetReadContext() const override;
    TIRDeviceValueContext GetWriteContext() const override;

    void InvalidateReadValues() override;

    std::string ToString() const;
    std::string Describe() const;
    PSerialDevice GetDevice() const;

private:
    TDeviceSetupItem(PSerialDevice device, PDeviceSetupItemConfig config);
    void Initialize();
};
