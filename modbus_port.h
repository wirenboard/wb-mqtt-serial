#pragma once
#include <memory>
#include <unordered_map>

#include "modbus_config.h"

class TMQTTWrapper;

class TModbusPort
{
public:
    TModbusPort(TMQTTWrapper* wrapper, const TPortConfig& port_config);
    void Cycle();
    void PubSubSetup();
    bool HandleMessage(const std::string& topic, const std::string& payload);
    std::string GetChannelTopic(const TModbusChannel& channel);
    bool WriteInitValues();
private:
    void OnModbusValueChange(const TModbusParameter& param, int int_value);
    TMQTTWrapper* Wrapper;
    TPortConfig Config;
    std::unique_ptr<TModbusClient> Client;
    std::unordered_map<TModbusParameter, TModbusChannel> ParameterToChannelMap;
    std::unordered_map<std::string, TModbusChannel> NameToChannelMap;
};
