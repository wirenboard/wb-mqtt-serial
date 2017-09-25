#pragma once
#include <memory>
#include <unordered_map>

#include <wbmqtt/driver.h>
#include "serial_config.h"
#include "serial_client.h"
#include "register_handler.h"
#include <chrono>


struct TDeviceChannel : public TDeviceChannelConfig
{
    TDeviceChannel(PSerialDevice device, PDeviceChannelConfig config)
        : TDeviceChannelConfig(*config)
        , Device(device)
    {
        for (const auto &reg_config: config->RegisterConfigs) {
            Registers.push_back(TRegister::Intern(device, reg_config));
        }
    }

    std::string Describe() const
    {
        return "channel '" + Name + "' of device '" + DeviceId + "'";
    }

    PSerialDevice Device;
    std::vector<PRegister> Registers;
};

typedef std::shared_ptr<TDeviceChannel> PDeviceChannel;

using TPublishTime = std::chrono::time_point<std::chrono::steady_clock>;
using PPublishTime = std::unique_ptr<TPublishTime>;

struct TDeviceChannelState
{
    TDeviceChannelState(const PDeviceChannel & channel, TRegisterHandler::TErrorState error, PPublishTime && time)
        : Channel(channel)
        , ErrorState(error)
        , LastPublishTime(std::move(time))
    {}

    PDeviceChannel                  Channel;
    TRegisterHandler::TErrorState   ErrorState;
    PPublishTime                    LastPublishTime;
};


class TSerialPortDriver
{
public:
    TSerialPortDriver(WBMQTT::PDeviceDriver mqttDriver, PPortConfig port_config, PAbstractSerialPort port_override = 0);
    ~TSerialPortDriver();
    void Cycle();
    void HandleControlOnValueEvent(const WBMQTT::TControlOnValueEvent & event);
    bool WriteInitValues();
    void ClearDevices();

private:
    WBMQTT::TLocalDeviceArgs From(const PSerialDevice & device);
    WBMQTT::TControlArgs From(const PDeviceChannel & channel);

    bool NeedToPublish(PRegister reg, bool changed);
    void OnValueRead(PRegister reg, bool changed);
    TRegisterHandler::TErrorState RegErrorState(PRegister reg);
    void UpdateError(PRegister reg, TRegisterHandler::TErrorState errorState);

    WBMQTT::PDeviceDriver MqttDriver;
    PPortConfig Config;
    PAbstractSerialPort Port;
    PSerialClient SerialClient;
    std::vector<PSerialDevice> Devices;

    std::unordered_map<PRegister, TDeviceChannelState> RegisterToChannelStateMap;
};

typedef std::shared_ptr<TSerialPortDriver> PSerialPortDriver;

struct TDeviceLinkData
{
    TSerialPortDriver*  PortDriver;
    TSerialDevice*      SerialDevice;
};

struct TControlLinkData
{
    TSerialPortDriver*  PortDriver;
    TDeviceChannel*     DeviceChannel;
};
