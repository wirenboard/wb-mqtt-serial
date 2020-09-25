#pragma once

#include "serial_config.h"
#include "serial_port_driver.h"

#include <wblib/declarations.h>

#include <vector>
#include <memory>
#include <atomic>


class TMQTTSerialDriver
{
public:
    TMQTTSerialDriver(WBMQTT::PDeviceDriver mqtt_driver, PHandlerConfig handler_config, PPort port_override = 0);
    void LoopOnce();
    void ClearDevices();

    void Start();
    void Stop();

private:
    std::vector<PSerialPortDriver> PortDrivers;
    std::vector<std::thread>       PortLoops;
    std::mutex                     ActiveMutex;
    bool                           Active;
};

typedef std::shared_ptr<TMQTTSerialDriver> PMQTTSerialDriver;
