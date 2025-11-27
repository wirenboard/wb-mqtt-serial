/* vim: set ts=4 sw=4: */

#pragma once

#include "serial_config.h"
#include "serial_device.h"
#include <memory>
#include <stdint.h>

class TPulsarDevice: public TSerialDevice, public TUInt32SlaveId
{
public:
    enum RegisterType
    {
        REG_DEFAULT,
        REG_SYSTIME
    };

    TPulsarDevice(PDeviceConfig config, PProtocol protocol);

    static void Register(TSerialDeviceFactory& factory);

    TRegisterValue ReadRegisterImpl(TPort& port, const TRegisterConfig& reg) override;

private:
    void WriteBCD(uint64_t data, uint8_t* buffer, size_t size, bool big_endian = true);
    void WriteHex(uint64_t data, uint8_t* buffer, size_t size, bool big_endian = true);

    uint64_t ReadBCD(const uint8_t* data, size_t size, bool big_endian = true);
    uint64_t ReadHex(const uint8_t* data, size_t size, bool big_endian = true);

    uint16_t CalculateCRC16(const uint8_t* buffer, size_t size);

    void WriteDataRequest(TPort& port, uint32_t addr, uint32_t mask, uint16_t id);
    void WriteSysTimeRequest(TPort& port, uint32_t addr, uint16_t id);

    void ReadResponse(TPort& port, uint32_t addr, uint8_t* payload, size_t size, uint16_t id);

    TRegisterValue ReadDataRegister(TPort& port, const TRegisterConfig& reg);
    TRegisterValue ReadSysTimeRegister(TPort& port, const TRegisterConfig& reg);

    uint16_t RequestID;
};

typedef std::shared_ptr<TPulsarDevice> PPulsarDevice;
