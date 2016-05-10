#pragma once

#include <string>
#include <memory>
#include <exception>
#include <stdint.h>
#include "serial_device.h"

class TIVTMDevice: public TSerialDevice {
public:
    TIVTMDevice(PDeviceConfig device_config, PAbstractSerialPort port);
    uint64_t ReadRegister(PRegister reg);
    void WriteRegister(PRegister reg, uint64_t value);

private:
    void WriteCommand(uint16_t addr, uint16_t data_addr, uint8_t data_len);
    void ReadResponse(uint16_t addr, uint8_t* payload, uint16_t len);

    uint8_t DecodeASCIIByte(uint8_t * buf);
    uint16_t DecodeASCIIWord(uint8_t * buf);
    bool DecodeASCIIBytes(uint8_t * buf, uint8_t* result, uint8_t len_bytes);
};

typedef std::shared_ptr<TIVTMDevice> PIVTMDevice;
