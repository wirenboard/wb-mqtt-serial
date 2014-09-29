#pragma once

#include <vector>
#include <memory>

#include "common/mqtt_wrapper.h"
#include "modbus_config.h"
#include "modbus_port.h"

class TMQTTModbusObserver : public IMQTTObserver,
                            public std::enable_shared_from_this<TMQTTModbusObserver>
{
public:
    TMQTTModbusObserver(PMQTTClientBase mqtt_client,
                        PHandlerConfig handler_config,
                        PModbusConnector connector = 0);

    void OnConnect(int rc);
    void OnMessage(const struct mosquitto_message *message);
    void OnSubscribe(int mid, int qos_count, const int *granted_qos);

    void ModbusLoopOnce();
    void ModbusLoop();
    bool WriteInitValues();
private:
    PMQTTClientBase MQTTClient;
    PHandlerConfig Config;
    std::vector<std::unique_ptr<TModbusPort>> Ports;
};

typedef std::shared_ptr<TMQTTModbusObserver> PMQTTModbusObserver;
