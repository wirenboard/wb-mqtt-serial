#pragma once

#include "serial_port_driver.h"

class TMQTTSerialDriver
{
public:
    TMQTTSerialDriver(WBMQTT::PDeviceDriver mqtt_driver, PHandlerConfig handler_config);
    void LoopOnce();
    void ClearDevices();

    void Start();
    void Stop();

    std::vector<PSerialPortDriver> GetPortDrivers();

private:
    std::vector<PSerialPortDriver> PortDrivers;
    std::vector<std::thread> PortLoops;
    std::mutex ActiveMutex;
    bool Active;
};

typedef std::shared_ptr<TMQTTSerialDriver> PMQTTSerialDriver;
