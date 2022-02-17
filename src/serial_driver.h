#pragma once

#include "serial_config.h"
#include "serial_port_driver.h"

#include <wblib/declarations.h>
#include <wblib/rpc.h>

class TMQTTSerialDriver
{
public:
    TMQTTSerialDriver(WBMQTT::PDeviceDriver mqtt_driver,
                      PHandlerConfig handler_config,
                      WBMQTT::PMqttRpcServer rpc = nullptr);
    void LoopOnce();
    void ClearDevices();

    void Start();
    void Stop();

    bool GetPortDriverByName(const std::string& path, const std::string& ip, int port, PSerialPortDriver& portDriver);

private:
    std::vector<PSerialPortDriver> PortDrivers;
    std::vector<std::thread> PortLoops;
    std::mutex ActiveMutex;
    bool Active;
    std::map<std::string, Metrics::TMetrics> Metrics;

    Json::Value LoadMetrics(const Json::Value& request);
};

typedef std::shared_ptr<TMQTTSerialDriver> PMQTTSerialDriver;
