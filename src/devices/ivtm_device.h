#pragma once

#include "serial_config.h"
#include "serial_device.h"
#include <exception>
#include <memory>
#include <stdint.h>
#include <string>

class TIVTMDevice: public TSerialDevice, public TUInt32SlaveId
{
public:
    TIVTMDevice(PDeviceConfig config, PPort port, PProtocol protocol);

    static void Register(TSerialDeviceFactory& factory);

    uint64_t ReadRegisterImpl(PRegister reg) override;

private:
    void WriteCommand(uint16_t addr, uint16_t data_addr, uint8_t data_len);
    void ReadResponse(uint16_t addr, uint8_t* payload, uint16_t len);

    uint8_t DecodeASCIIByte(uint8_t* buf);
    uint16_t DecodeASCIIWord(uint8_t* buf);
    bool DecodeASCIIBytes(uint8_t* buf, uint8_t* result, uint8_t len_bytes);
};

typedef std::shared_ptr<TIVTMDevice> PIVTMDevice;
