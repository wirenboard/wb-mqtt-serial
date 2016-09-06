#pragma once

#include <string>
#include <memory>
#include <exception>
#include <cstdint>

#include "em_device.h"

class TMilurDevice;
typedef TBasicProtocol<TMilurDevice> TMilurProtocol;

class TMilurDevice: public TEMDevice<TMilurProtocol> {
public:
    static const int DefaultTimeoutMs = 1000;
    static const int FrameTimeoutMs = 50;
    enum RegisterType {
        REG_PARAM = 0,
        REG_POWER,
        REG_ENERGY,
        REG_FREQ,
        REG_POWERFACTOR
    };

    TMilurDevice(PDeviceConfig device_config, PAbstractSerialPort port, PProtocol protocol);
    uint64_t ReadRegister(PRegister reg);
    void Prepare();


protected:
    bool ConnectionSetup(uint8_t slave);
    ErrorType CheckForException(uint8_t* frame, int len, const char** message);
};

typedef std::shared_ptr<TMilurDevice> PMilurDevice;
