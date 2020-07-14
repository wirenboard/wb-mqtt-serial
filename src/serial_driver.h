#pragma once

#include "serial_config.h"
#include "serial_port_driver.h"

#include <wblib/declarations.h>

#include <vector>
#include <memory>


class TMQTTSerialDriver
{
public:
    TMQTTSerialDriver(WBMQTT::PDeviceDriver mqtt_driver, PHandlerConfig handler_config, PAbstractSerialPort port_override = 0);
    void LoopOnce();
    void ClearDevices();

    void Start();
    void Stop();

    bool WriteInitValues();

private:
    std::vector<PSerialPortDriver> PortDrivers;
    std::vector<std::thread>       PortLoops;
    std::atomic_bool               Active;
};

typedef std::shared_ptr<TMQTTSerialDriver> PMQTTSerialDriver;
