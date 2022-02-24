#pragma once
#include "register_handler.h"
#include "serial_client.h"
#include "serial_config.h"

#include <wblib/declarations.h>

#include <chrono>
#include <memory>
#include <unordered_map>

struct TDeviceChannel: public TDeviceChannelConfig
{
    TDeviceChannel(PSerialDevice device, PDeviceChannelConfig config): TDeviceChannelConfig(*config), Device(device)
    {
        for (const auto& reg_config: config->RegisterConfigs) {
            Registers.push_back(TRegister::Intern(device, reg_config, config->MqttId));
        }
    }

    std::string Describe() const
    {
        const auto& name = GetName();
        if (name != MqttId) {
            return "channel '" + name + "' (MQTT control '" + MqttId + "') of device '" + DeviceId + "'";
        }
        return "channel '" + name + "' of device '" + DeviceId + "'";
    }

    void UpdateValueAndError(WBMQTT::TDeviceDriver& deviceDriver, const WBMQTT::TPublishParameters& publishPolicy);
    void UpdateError(WBMQTT::TDeviceDriver& deviceDriver);

    bool HasValuesOfAllRegisters() const;

    PSerialDevice Device;
    std::vector<PRegister> Registers;
    WBMQTT::PControl Control;

private:
    std::string GetTextValue() const;
    std::string GetErrorText() const;
    void PublishValueAndError(WBMQTT::TDeviceDriver& deviceDriver, const std::string& value, const std::string& error);
    /* Current value of a channel, error flag and last update time.
       They are used to prevent unnecessary calls to libwbmqtt1.
       Although libwbmqtt1 implements publishing control with TPublishParams,
       it is very expensive to call it on every channel update.
       So we implement publish control logic in wb-mqtt-serial until libwbmqtt1 is fixed.
    */
    std::string CachedCurrentValue;
    std::string CachedErrorText;
    std::chrono::steady_clock::time_point LastControlUpdate;
};

typedef std::shared_ptr<TDeviceChannel> PDeviceChannel;

class TSerialPortDriver: public std::enable_shared_from_this<TSerialPortDriver>
{
public:
    TSerialPortDriver(WBMQTT::PDeviceDriver mqttDriver,
                      PPortConfig port_config,
                      const WBMQTT::TPublishParameters& publishPolicy,
                      Metrics::TMetrics& metrics);

    void SetUpDevices();
    void Cycle();
    void ClearDevices() noexcept;

    const std::string& GetShortDescription() const;

    static void HandleControlOnValueEvent(const WBMQTT::TControlOnValueEvent& event);

    bool RPCTransieve(std::vector<uint8_t>& buf,
                      size_t responseSize,
                      std::chrono::microseconds respTimeout,
                      std::chrono::microseconds frameTimeout,
                      std::chrono::seconds totalTimeout,
                      std::vector<uint8_t>& response,
                      size_t& actualResponseSize);

    Json::Value GetPortName();

private:
    WBMQTT::TLocalDeviceArgs From(const PSerialDevice& device);
    WBMQTT::TControlArgs From(const PDeviceChannel& channel);

    void SetValueToChannel(const PDeviceChannel& channel, const std::string& value);
    void OnValueRead(PRegister reg);
    void UpdateError(PRegister reg);

    WBMQTT::PDeviceDriver MqttDriver;
    PPortConfig Config;
    PSerialClient SerialClient;
    std::vector<PSerialDevice> Devices;
    std::string Description;
    WBMQTT::TPublishParameters PublishPolicy;

    std::unordered_map<PRegister, PDeviceChannel> RegisterToChannelMap;
};

typedef std::shared_ptr<TSerialPortDriver> PSerialPortDriver;

struct TControlLinkData
{
    std::weak_ptr<TSerialPortDriver> PortDriver;
    std::weak_ptr<TDeviceChannel> DeviceChannel;
};
