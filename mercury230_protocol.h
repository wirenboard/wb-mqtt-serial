#pragma once

#include <list>
#include <vector>
#include <string>
#include <memory>
#include <exception>
#include <unordered_map>
#include <cstdint>

#include "em_protocol.h"

class TMercury230Protocol: public TEMProtocol {
public:
    static const int DefaultTimeoutMs = 1000;
    enum RegisterType {
        REG_VALUE_ARRAY = 0,
        REG_PARAM = 1
    };

    TMercury230Protocol(PDeviceConfig, PAbstractSerialPort port);
    uint64_t ReadRegister(PRegister reg);
    void EndPollCycle();

protected:
    bool ConnectionSetup(uint8_t slave);
    ErrorType CheckForException(uint8_t* frame, int len, const char** message);

private:
    struct TValueArray {
        uint32_t values[4];
    };
    const TValueArray& ReadValueArray(uint32_t slave, uint32_t address);
    uint32_t ReadParam(uint32_t slave, uint32_t address);

    std::unordered_map<int, TValueArray> CachedValues;
};

typedef std::shared_ptr<TMercury230Protocol> PMercury230Protocol;
