#pragma once

#include <string>
#include <memory>
#include <exception>
#include <unordered_map>
#include <stdint.h>

#include "serial_protocol.h"
#include "regformat.h"

class TMilurProtocol: public TSerialProtocol {
public:
    static const int DefaultTimeoutMs = 1000;

    TMilurProtocol(PAbstractSerialPort port);
    uint64_t ReadRegister(uint8_t slave, uint8_t address, RegisterFormat fmt);
    void WriteRegister(uint8_t mod, uint8_t address, uint64_t value, RegisterFormat fmt);
    // XXX FIXME: leaky abstraction (need to refactor)
    // Perhaps add 'brightness' register format
    void SetBrightness(uint8_t mod, uint8_t address, uint8_t value);

private:
    void EnsureSlaveConnected(uint8_t slave);
    void WriteCommand(uint8_t slave, uint8_t cmd, uint8_t* payload, int len);
    void ReadResponse(uint8_t slave, uint8_t cmd, uint8_t* payload, int len);

    std::unordered_map<uint8_t, bool> slaveMap;

    const int MAX_LEN = 32;
    const uint8_t ACCESS_LEVEL = 1;
    const int N_CONN_ATTEMPTS = 10;
};

typedef std::shared_ptr<TMilurProtocol> PMilurProtocol;
