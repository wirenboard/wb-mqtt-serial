#pragma once
#include <memory>
#include <unordered_map>

#include "common/mqtt_wrapper.h"
#include "modbus_config.h"

class TMQTTWrapper;

class TModbusPort
{
public:
    TModbusPort(PMQTTClientBase mqtt_client, PPortConfig port_config, PModbusConnector connector);
    void Cycle();
    void PubSubSetup();
    bool HandleMessage(const std::string& topic, const std::string& payload);
    std::string GetChannelTopic(const TModbusChannel& channel);
    bool WriteInitValues();
private:
    void OnModbusValueChange(const TModbusRegister& reg);
    PMQTTClientBase MQTTClient;
    PPortConfig Config;
    std::unique_ptr<TModbusClient> ModbusClient;
    std::unordered_map<TModbusRegister, PModbusChannel> RegisterToChannelMap;
    std::unordered_map<std::string, PModbusChannel> NameToChannelMap;
};
