#pragma once

#include <exception>
#include <memory>
#include <stdint.h>
#include <string>

#include "serial_device.h"

#include "modbus_common.h"

template<class Dev> class TModbusDeviceFactory: public IDeviceFactory
{
    std::unique_ptr<Modbus::IModbusTraitsFactory> ModbusTraitsFactory;

public:
    TModbusDeviceFactory(std::unique_ptr<Modbus::IModbusTraitsFactory> modbusTraitsFactory)
        : IDeviceFactory(std::make_unique<TUint32RegisterAddressFactory>(2),
                         "#/definitions/simple_device_with_setup",
                         "#/definitions/common_channel"),
          ModbusTraitsFactory(std::move(modbusTraitsFactory))
    {}

    PSerialDevice CreateDevice(const Json::Value& data,
                               PDeviceConfig deviceConfig,
                               PPort port,
                               PProtocol protocol) const override
    {
        auto dev = std::make_shared<Dev>(ModbusTraitsFactory->GetModbusTraits(port), deviceConfig, port, protocol);
        dev->InitSetupItems();
        return dev;
    }
};

class TModbusDevice: public TSerialDevice, public TUInt32SlaveId
{
    std::unique_ptr<Modbus::IModbusTraits> ModbusTraits;
    Modbus::TRegisterCache ModbusCache;

public:
    TModbusDevice(std::unique_ptr<Modbus::IModbusTraits> modbusTraits,
                  PDeviceConfig config,
                  PPort port,
                  PProtocol protocol);

    PRegisterRange CreateRegisterRange() const override;
    void ReadRegisterRange(PRegisterRange range) override;
    void WriteSetupRegisters() override;

    static void Register(TSerialDeviceFactory& factory);

protected:
    void WriteRegisterImpl(PRegister reg, const TChannelValue& value) override;
};
