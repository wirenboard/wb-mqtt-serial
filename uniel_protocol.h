#pragma once

#include <string>
#include <memory>
#include <exception>
#include <stdint.h>

#include "serial_protocol.h"
#include "regformat.h"

class TUnielProtocol: public TSerialProtocol {
public:
    static const int DefaultTimeoutMs = 1000;

    TUnielProtocol(PAbstractSerialPort port);
    uint64_t ReadRegister(uint32_t mod, uint32_t address, RegisterFormat fmt, size_t width);
    void WriteRegister(uint32_t mod, uint32_t address, uint64_t value, RegisterFormat fmt);
    void SetBrightness(uint32_t mod, uint32_t address, uint8_t value);

private:
    void WriteCommand(uint8_t cmd, uint8_t mod, uint8_t b1, uint8_t b2, uint8_t b3);
    void ReadResponse(uint8_t cmd, uint8_t* response);
    void DoWriteRegister(uint8_t cmd, uint8_t mod, uint8_t address, uint8_t value);
};
