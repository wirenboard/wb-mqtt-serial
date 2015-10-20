#pragma once

#include <string>
#include <exception>
#include <unordered_map>
#include <stdint.h>

#include "serialprotocol.h"

class TMilurProtocol: public TSerialProtocol {
public:
    static const int DefaultTimeoutMs = 1000;

    TMilurProtocol(const TSerialPortSettings& settings, bool debug = false);
    int ReadRegister(uint8_t slave, uint8_t address);

private:
    void EnsureSlaveConnected(uint8_t slave);
    void WriteCommand(uint8_t slave, uint8_t cmd, uint8_t* payload, int len);
    void ReadResponse(uint8_t slave, uint8_t cmd, uint8_t* payload, int len);

    std::unordered_map<uint8_t, bool> slaveMap;

    const int MAX_LEN = 32;
    const uint8_t ACCESS_LEVEL = 1;
    const int N_CONN_ATTEMPTS = 10;
};
