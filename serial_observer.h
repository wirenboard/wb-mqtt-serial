#pragma once

#include <vector>
#include <memory>

#include <wbmqtt/mqtt_wrapper.h>
#include "serial_config.h"
#include "serial_port_driver.h"

class TMQTTSerialObserver : public IMQTTObserver,
                            public std::enable_shared_from_this<TMQTTSerialObserver>
{
public:
    TMQTTSerialObserver(PMQTTClientBase mqtt_client,
                        PHandlerConfig handler_config,
                        PAbstractSerialPort port_override = 0);

    void SetUp();
    void OnConnect(int rc);
    void OnMessage(const struct mosquitto_message *message);
    void OnSubscribe(int mid, int qos_count, const int *granted_qos);

    void LoopOnce();
    void Loop();
    bool WriteInitValues();

private:
    PMQTTClientBase MQTTClient;
    PHandlerConfig Config;
    std::vector<PSerialPortDriver> PortDrivers;
};

typedef std::shared_ptr<TMQTTSerialObserver> PMQTTSerialObserver;


class TMQTTPrefixedClient : public TMQTTClient
{
    using Base = TMQTTClient;
public:
    TMQTTPrefixedClient(std::string prefix, const TConfig& config)
        : TMQTTClient(config)
        , Prefix(std::move(prefix))
    {}

    int Publish(int *mid, const std::string& topic, const std::string& payload="", int qos=0, bool retain=false) override;
    int Subscribe(int *mid, const std::string& sub, int qos=0) override;
    void on_message(const struct mosquitto_message *message) override;

private:
    std::string Prefix;
};
