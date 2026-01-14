#pragma once

#include <exception>
#include <memory>
#include <stdint.h>
#include <string>

#include "serial_config.h"
#include "serial_device.h"

#include "modbus_common.h"
#include "running_average.h"

class TModbusDeviceConfig
{
public:
    PDeviceConfig CommonConfig;

    /**
     * @brief The device can support continuous read feature implemented in WB devices
     *        https://wirenboard.com/wiki/Modbus#%D0%A0%D0%B5%D0%B6%D0%B8%D0%BC_%D1%81%D0%BF%D0%BB%D0%BE%D1%88%D0%BD%D0%BE%D0%B3%D0%BE_%D1%87%D1%82%D0%B5%D0%BD%D0%B8%D1%8F_%D1%80%D0%B5%D0%B3%D0%B8%D1%81%D1%82%D1%80%D0%BE%D0%B2
     *
     */
    bool EnableWbContinuousRead = false;
};

template<class Dev> class TModbusDeviceFactory: public IDeviceFactory
{
    std::unique_ptr<Modbus::IModbusTraitsFactory> ModbusTraitsFactory;

public:
    TModbusDeviceFactory(std::unique_ptr<Modbus::IModbusTraitsFactory> modbusTraitsFactory)
        : IDeviceFactory(std::make_unique<TUint32RegisterAddressFactory>(2),
                         "#/definitions/simple_device_with_setup",
                         "#/definitions/common_channel_modbus"),
          ModbusTraitsFactory(std::move(modbusTraitsFactory))
    {}

    PSerialDevice CreateDevice(const Json::Value& data, PDeviceConfig deviceConfig, PProtocol protocol) const override
    {
        TModbusDeviceConfig config;
        config.CommonConfig = deviceConfig;
        WBMQTT::JSON::Get(data, "enable_wb_continuous_read", config.EnableWbContinuousRead);
        bool forceFrameTimeout = false;
        WBMQTT::JSON::Get(data, "force_frame_timeout", forceFrameTimeout);

        return std::make_shared<Dev>(ModbusTraitsFactory->GetModbusTraits(forceFrameTimeout), config, protocol);
    }
};

class TModbusDevice: public TSerialDevice, public TUInt32SlaveId
{
    std::unique_ptr<Modbus::IModbusTraits> ModbusTraits;
    Modbus::TRegisterCache ModbusCache;
    TRunningAverage<std::chrono::microseconds, 10> ResponseTime;
    bool EnableWbContinuousRead;
    bool ContinuousReadEnabled;
    std::string FwVersion;

public:
    TModbusDevice(std::unique_ptr<Modbus::IModbusTraits> modbusTraits,
                  const TModbusDeviceConfig& config,
                  PProtocol protocol);

    bool GetForceFrameTimeout();
    bool GetContinuousReadEnabled();

    PRegisterRange CreateRegisterRange() const override;
    void ReadRegisterRange(TPort& port, PRegisterRange range, bool breakOnError = false) override;
    void WriteSetupRegisters(TPort& port, const TDeviceSetupItems& setupItems, bool breakOnError = false) override;

    std::chrono::milliseconds GetFrameTimeout(TPort& port) const override;

    static void Register(TSerialDeviceFactory& factory);

protected:
    void PrepareImpl(TPort& port) override;
    void WriteRegisterImpl(TPort& port, const TRegisterConfig& reg, const TRegisterValue& value) override;
};
