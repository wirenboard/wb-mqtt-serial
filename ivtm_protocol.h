#pragma once

#include <string>
#include <memory>
#include <exception>
#include <stdint.h>
#include "serial_protocol.h"
#include "regformat.h"

class TIVTMProtocol: public TSerialProtocol {
public:
    static const int DefaultTimeoutMs = 1000;
    static const int FrameTimeoutMs = 50;

    TIVTMProtocol(PAbstractSerialPort port);
    uint64_t ReadRegister(uint32_t mod, uint32_t address, RegisterFormat fmt, size_t width);
    void WriteRegister(uint32_t mod, uint32_t address, uint64_t value, RegisterFormat fmt);
    void SetBrightness(uint32_t mod, uint32_t address, uint8_t value);

private:
    void WriteCommand(uint16_t addr, uint16_t data_addr, uint8_t data_len);
    void ReadResponse(uint16_t addr, uint8_t* payload, uint16_t len);

    uint8_t DecodeASCIIByte(uint8_t * buf);
    uint16_t DecodeASCIIWord(uint8_t * buf);
    bool DecodeASCIIBytes(uint8_t * buf, uint8_t* result, uint8_t len_bytes);
};
