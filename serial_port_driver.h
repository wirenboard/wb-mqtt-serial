#pragma once
#include <memory>
#include <unordered_map>

#include <wbmqtt/mqtt_wrapper.h>
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

    PSerialDevice Device;
    std::vector<PRegister> Registers;
};

typedef std::shared_ptr<TDeviceChannel> PDeviceChannel;


struct TDeviceSetupItem : public TDeviceSetupItemConfig
{
    TDeviceSetupItem(PSerialDevice device, PDeviceSetupItemConfig config)
        : TDeviceSetupItemConfig(*config)
        , Device(device)
    {
        Register = TRegister::Intern(device, config->RegisterConfig);    
    }

    PSerialDevice Device;
    PRegister Register;
};

typedef std::shared_ptr<TDeviceSetupItem> PDeviceSetupItem;


class TMQTTWrapper;

class TSerialPortDriver
{
public:
    TSerialPortDriver(PMQTTClientBase mqtt_client, PPortConfig port_config, PAbstractSerialPort port_override = 0);
    ~TSerialPortDriver();
    void Cycle();
    void PubSubSetup();
    bool HandleMessage(const std::string& topic, const std::string& payload);
    std::string GetChannelTopic(const TDeviceChannelConfig& channel);
    bool WriteInitValues();

private:
    bool NeedToPublish(PRegister reg, bool changed);
    void OnValueRead(PRegister reg, bool changed);
    TRegisterHandler::TErrorState RegErrorState(PRegister reg);
    void UpdateError(PRegister reg, TRegisterHandler::TErrorState errorState);

    PMQTTClientBase MQTTClient;
    PPortConfig Config;
    PAbstractSerialPort Port;
    PSerialClient SerialClient;
    std::vector<PSerialDevice> Devices;
    std::vector<PDeviceSetupItem> SetupItems;

    std::unordered_map<PRegister, PDeviceChannel> RegisterToChannelMap;
    std::unordered_map<PDeviceChannelConfig, std::vector<PRegister>> ChannelRegistersMap;
    std::unordered_map<PRegister, TRegisterHandler::TErrorState> RegErrorStateMap;
    std::unordered_map<PRegister, std::chrono::time_point<std::chrono::steady_clock>> RegLastPublishTimeMap;
    std::unordered_map<std::string, std::string> PublishedErrorMap;
    std::unordered_map<std::string, PDeviceChannel> NameToChannelMap;
};

typedef std::shared_ptr<TSerialPortDriver> PSerialPortDriver;
