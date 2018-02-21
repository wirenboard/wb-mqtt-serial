#pragma once

#include "virtual_register.h"

#include <memory>
#include <unordered_map>

#include <wbmqtt/mqtt_wrapper.h>
#include "serial_config.h"
#include "serial_client.h"
#include "register_handler.h"
#include <chrono>


struct TDeviceChannel : public TDeviceChannelConfig
{
    TDeviceChannel(PSerialDevice device, PDeviceChannelConfig config, TVirtualRegister::TInitContext & context)
        : TDeviceChannelConfig(*config)
        , Device(device)
    {
        for (const auto &reg_config: config->RegisterConfigs) {
            Registers.push_back(TVirtualRegister::Create(reg_config, device, context));
        }
    }

    PSerialDevice Device;
    std::vector<PVirtualRegister> Registers;
};

typedef std::shared_ptr<TDeviceChannel> PDeviceChannel;


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
    bool NeedToPublish(const PVirtualRegister & reg, bool changed);
    void OnValueRead(const PVirtualRegister & reg, bool changed);
    void UpdateError(const PVirtualRegister & reg);

    PMQTTClientBase MQTTClient;
    PPortConfig Config;
    PPort Port;
    PSerialClient SerialClient;
    std::vector<PSerialDevice> Devices;

    std::unordered_map<PVirtualRegister, PDeviceChannel> RegisterToChannelMap;
    std::unordered_map<PDeviceChannelConfig, std::vector<PVirtualRegister>> ChannelRegistersMap;
    std::unordered_map<PVirtualRegister, std::chrono::time_point<std::chrono::steady_clock>> RegLastPublishTimeMap;
    std::unordered_map<std::string, std::string> PublishedErrorMap;
    std::unordered_map<std::string, PDeviceChannel> NameToChannelMap;
};

typedef std::shared_ptr<TSerialPortDriver> PSerialPortDriver;
