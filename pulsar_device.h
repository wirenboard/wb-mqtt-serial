/* vim: set ts=4 sw=4: */

#pragma once

#include <memory>
#include <stdint.h>
#include "serial_device.h"


class TPulsarDevice: public TBasicProtocolSerialDevice<TBasicProtocol<TPulsarDevice>> {
public:

    enum RegisterType {
        REG_DEFAULT,
        REG_SYSTIME
    };

    TPulsarDevice(PDeviceConfig device_config, PPort port, PProtocol protocol);

    std::vector<uint8_t> ReadMemoryBlock(const PMemoryBlock & mb) override;
    void WriteMemoryBlock(const PMemoryBlock & mb, const std::vector<uint8_t> &) override;

private:
    void WriteBCD(uint64_t data, uint8_t *buffer, size_t size, bool big_endian = true);
    void WriteHex(uint64_t data, uint8_t *buffer, size_t size, bool big_endian = true);

    uint64_t ReadBCD(const uint8_t *data, size_t size, bool big_endian = true);
    uint64_t ReadHex(const uint8_t *data, size_t size, bool big_endian = true);

    uint16_t CalculateCRC16(const uint8_t *buffer, size_t size);

    void WriteDataRequest(uint32_t addr, uint32_t mask, uint16_t id);
    void WriteSysTimeRequest(uint32_t addr, uint16_t id);

    void ReadResponse(uint32_t addr, std::vector<uint8_t> & payload, uint16_t id);

    std::vector<uint8_t> ReadDataRegister(const PMemoryBlock & mb);
    std::vector<uint8_t> ReadSysTimeRegister(const PMemoryBlock & mb);

    uint16_t RequestID;
};

typedef std::shared_ptr<TPulsarDevice> PPulsarDevice;
