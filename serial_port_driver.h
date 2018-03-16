#pragma once

#include "serial_config.h"
#include "serial_client.h"

#include <wbmqtt/mqtt_wrapper.h>

#include <memory>
#include <unordered_map>
#include <chrono>


struct TDeviceChannel : public TDeviceChannelConfig
{
    TDeviceChannel(PSerialDevice device, PDeviceChannelConfig config, PAbstractVirtualRegister reg)
        : TDeviceChannelConfig(*config)
        , Device(device)
        , Register(reg)
    {}

    PSerialDevice Device;
    PAbstractVirtualRegister Register;
};


class TMQTTWrapper;

class TSerialPortDriver
{
public:
    TSerialPortDriver(PMQTTClientBase mqtt_client, PPortConfig port_config, PPort port_override = 0);
    ~TSerialPortDriver();
    void Cycle();
    void PubSubSetup();
    bool HandleMessage(const std::string& topic, const std::string& payload);
    std::string GetChannelTopic(const TDeviceChannelConfig& channel);
    bool WriteInitValues();

private:
    bool NeedToPublish(const PAbstractVirtualRegister & reg);
    void OnValueRead(const PVirtualRegister & reg);
    void UpdateError(const PVirtualRegister & reg);

    PMQTTClientBase MQTTClient;
    PPortConfig Config;
    PPort Port;
    PSerialClient SerialClient;
    std::vector<PSerialDevice> Devices;

    std::unordered_map<PAbstractVirtualRegister, PDeviceChannel> RegisterToChannelMap;
    std::unordered_map<PDeviceChannelConfig, PAbstractVirtualRegister> ChannelRegisterMap;
    std::unordered_map<PAbstractVirtualRegister, std::chrono::time_point<std::chrono::steady_clock>> RegLastPublishTimeMap;
    std::unordered_map<std::string, std::string> PublishedErrorMap;
    std::unordered_map<std::string, PDeviceChannel> NameToChannelMap;
};

typedef std::shared_ptr<TSerialPortDriver> PSerialPortDriver;
