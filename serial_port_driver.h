#pragma once
#include <memory>
#include <unordered_map>

#include <wbmqtt/mqtt_wrapper.h>
#include "serial_config.h"
#include "serial_client.h"
#include "register_handler.h"
#include <chrono>

class TMQTTWrapper;

class TSerialPortDriver
{
public:
    TSerialPortDriver(PMQTTClientBase mqtt_client, PPortConfig port_config, PAbstractSerialPort port_override = 0);
    void Cycle();
    void PubSubSetup();
    bool HandleMessage(const std::string& topic, const std::string& payload);
    std::string GetChannelTopic(const TDeviceChannel& channel);
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
    std::unordered_map<PRegister, PDeviceChannel> RegisterToChannelMap;
    std::unordered_map<PRegister, TRegisterHandler::TErrorState> RegErrorStateMap;
    std::unordered_map<PRegister, std::chrono::time_point<std::chrono::steady_clock>> RegLastPublishTimeMap;
    std::unordered_map<std::string, std::string> PublishedErrorMap;
    std::unordered_map<std::string, PDeviceChannel> NameToChannelMap;
};

typedef std::shared_ptr<TSerialPortDriver> PSerialPortDriver;
