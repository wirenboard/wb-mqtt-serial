#pragma once

#include "serial_config.h"
#include "serial_port_driver.h"

#include <wblib/declarations.h>
#include <wblib/rpc.h>

class TMQTTSerialDriver
{
public:
    TMQTTSerialDriver(WBMQTT::PDeviceDriver mqtt_driver, PHandlerConfig handler_config);
    void LoopOnce();
    void ClearDevices();

    void Start();
    void Stop();

    void RPCGetMetrics(Json::Value& metrics);
    std::vector<PSerialPortDriver> GetPortDrivers();

private:
    std::vector<PSerialPortDriver> PortDrivers;
    std::vector<std::thread> PortLoops;
    std::mutex ActiveMutex;
    bool Active;
    std::map<std::string, Metrics::TMetrics> Metrics;
};

typedef std::shared_ptr<TMQTTSerialDriver> PMQTTSerialDriver;
