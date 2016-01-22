#pragma once
#include <memory>
#include <unordered_map>

#include <wbmqtt/mqtt_wrapper.h>
#include "modbus_config.h"
#include "modbus_client.h"

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
    void OnModbusValueChange(PModbusRegister reg);
    TModbusClient::TErrorState RegErrorState(PModbusRegister reg);
    void UpdateError(PModbusRegister reg, TModbusClient::TErrorState errorState);
    PMQTTClientBase MQTTClient;
    PPortConfig Config;
    std::unique_ptr<TModbusClient> ModbusClient;
    std::unordered_map<PModbusRegister, PModbusChannel> RegisterToChannelMap;
    std::unordered_map<PModbusRegister, TModbusClient::TErrorState> RegErrorStateMap;
    std::unordered_map<std::string, std::string> PublishedErrorMap;
    std::unordered_map<std::string, PModbusChannel> NameToChannelMap;
};
