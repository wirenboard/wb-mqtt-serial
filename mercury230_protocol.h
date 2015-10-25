#pragma once

#include <list>
#include <vector>
#include <string>
#include <memory>
#include <exception>
#include <unordered_map>
#include <cstdint>

#include "em_protocol.h"
#include "regformat.h"

class TMercury230Protocol: public TEMProtocol {
public:
    static const int DefaultTimeoutMs = 1000;

    TMercury230Protocol(PAbstractSerialPort port);
    uint64_t ReadRegister(uint32_t slave, uint32_t address, RegisterFormat fmt);
    void EndPollCycle();

protected:
    bool ConnectionSetup(uint8_t slave);

private:
    struct TValueArray {
        uint32_t values[4];
    };
    const TValueArray& ReadValueArray(uint32_t slave, uint32_t address);

    const uint8_t ACCESS_LEVEL = 1;
    std::unordered_map<int, TValueArray> CachedValues;
};

typedef std::shared_ptr<TMercury230Protocol> PMercury230Protocol;
