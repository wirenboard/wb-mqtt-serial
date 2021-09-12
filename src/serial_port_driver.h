#pragma once
#include "serial_config.h"
#include "serial_client.h"
#include "register_handler.h"

#include <wblib/declarations.h>

#include <chrono>
#include <memory>
#include <unordered_map>


struct TDeviceChannel : public TDeviceChannelConfig
{
    TDeviceChannel(PSerialDevice device, PDeviceChannelConfig config)
        : TDeviceChannelConfig(*config)
        , Device(device)
    {
        for (const auto &reg_config: config->RegisterConfigs) {
            Registers.push_back(TRegister::Intern(device, reg_config, config->MqttId));
        }
    }

    std::string Describe() const
    {
        if (Name != MqttId) {
            return "channel '" + Name + "' (MQTT control '" + MqttId + "') of device '" + DeviceId + "'";
        }
        return "channel '" + Name + "' of device '" + DeviceId + "'";
    }

    void UpdateError(WBMQTT::TDeviceDriver& deviceDriver, const std::string& error);
    void UpdateValue(WBMQTT::TDeviceDriver& deviceDriver, const WBMQTT::TPublishParameters& publishPolicy, const std::string& error);

    PSerialDevice Device;
    std::vector<PRegister> Registers;
    WBMQTT::PControl Control;

private:
    void PublishValue(WBMQTT::TDeviceDriver& deviceDriver, const std::string& value);
    /* Current value of a channel, error flag and last update time.
       They are used to prevent unnecessary calls to libwbmqtt1.
       Although libwbmqtt1 implements publishing control with TPublishParams,
       it is very expensive to call it on every channel update.
       So we implement publish control logic in wb-mqtt-serial until libwbmqtt1 is fixed.
    */
    std::string                           CachedCurrentValue;
    std::string                           CachedErrorFlg;
    std::chrono::steady_clock::time_point LastControlUpdate;
};

typedef std::shared_ptr<TDeviceChannel> PDeviceChannel;

struct TDeviceChannelState
{
    TDeviceChannelState(const PDeviceChannel & channel, TRegisterHandler::TErrorState error)
        : Channel(channel)
        , ErrorState(error)
    {}

    PDeviceChannel                  Channel;
    TRegisterHandler::TErrorState   ErrorState;
};


class TSerialPortDriver: public std::enable_shared_from_this<TSerialPortDriver>
{
public:
    TSerialPortDriver(WBMQTT::PDeviceDriver             mqttDriver,
                      PPortConfig                       port_config, 
                      const WBMQTT::TPublishParameters& publishPolicy,
                      Metrics::TMetrics&                metrics);

    void SetUpDevices();
    void Cycle();
    void ClearDevices() noexcept;

    const std::string& GetShortDescription() const;

    static void HandleControlOnValueEvent(const WBMQTT::TControlOnValueEvent & event);

private:
    WBMQTT::TLocalDeviceArgs From(const PSerialDevice & device);
    WBMQTT::TControlArgs From(const PDeviceChannel & channel);

    void SetValueToChannel(const PDeviceChannel & channel, const std::string & value);
    void OnValueRead(PRegister reg, bool changed);
    TRegisterHandler::TErrorState RegErrorState(PRegister reg);
    void UpdateError(PRegister reg, TRegisterHandler::TErrorState errorState);

    WBMQTT::PDeviceDriver      MqttDriver;
    PPortConfig                Config;
    PSerialClient              SerialClient;
    std::vector<PSerialDevice> Devices;
    std::string                Description;
    WBMQTT::TPublishParameters PublishPolicy;

    std::unordered_map<PRegister, TDeviceChannelState> RegisterToChannelStateMap;
};

typedef std::shared_ptr<TSerialPortDriver> PSerialPortDriver;

struct TControlLinkData
{
    std::weak_ptr<TSerialPortDriver>  PortDriver;
    std::weak_ptr<TDeviceChannel>     DeviceChannel;
};
