#pragma once

#include <vector>
#include <memory>

#include "common/mqtt_wrapper.h"
#include "modbus_config.h"
#include "modbus_port.h"

class TMQTTModbusHandler : public TMQTTWrapper
{
public:
    TMQTTModbusHandler(const TMQTTModbusHandler::TConfig& mqtt_config, const THandlerConfig& handler_config);

    void OnConnect(int rc);
    void OnMessage(const struct mosquitto_message *message);
    void OnSubscribe(int mid, int qos_count, const int *granted_qos);

    void ModbusLoop();
    bool WriteInitValues();
private:
    THandlerConfig Config;
    std::vector<std::unique_ptr<TModbusPort>> Ports;
};
