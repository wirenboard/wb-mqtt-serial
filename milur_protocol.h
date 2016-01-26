#pragma once

#include <string>
#include <memory>
#include <exception>
#include <cstdint>

#include "em_protocol.h"

class TMilurProtocol: public TEMProtocol {
public:
    static const int DefaultTimeoutMs = 1000;
    static const int FrameTimeoutMs = 50;
    enum RegisterType {
        REG_PARAM = 0,
        REG_POWER,
        REG_ENERGY,
        REG_FREQ
    };

    TMilurProtocol(PDeviceConfig device_config, PAbstractSerialPort port);
    uint64_t ReadRegister(PRegister reg);

protected:
    bool ConnectionSetup(uint8_t slave);
    ErrorType CheckForException(uint8_t* frame, int len, const char** message);
};

typedef std::shared_ptr<TMilurProtocol> PMilurProtocol;
