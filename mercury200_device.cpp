#include "mercury200_device.h"
#include "memory_block.h"
#include "bcd_utils.h"
#include "crc16.h"

#include <cstdint>
#include <chrono>

namespace
{
    const size_t RESPONSE_BUF_LEN = 100;
    const size_t REQUEST_LEN = 7;
    const ptrdiff_t HEADER_SZ = 5;
}

REGISTER_BASIC_INT_PROTOCOL("mercury200", TMercury200Device, TRegisterTypes({
    { TMercury200Device::MEM_TARIFFS, "tariffs", "value", { U32, U32, U32, U32 }, true },
    { TMercury200Device::MEM_PARAMS, "params", "value", { BCD16, BCD16, BCD24 }, true },
    { TMercury200Device::REG_PARAM_16, "param16", "value", { BCD16 }, true },
}));

TMercury200Device::TMercury200Device(PDeviceConfig config, PPort port, PProtocol protocol)
    : TBasicProtocolSerialDevice<TBasicProtocol<TMercury200Device>>(config, port, protocol)
{}

TMercury200Device::~TMercury200Device()
{}

std::vector<uint8_t> TMercury200Device::ExecCommand(uint8_t cmd)
{
    uint8_t buf[100] = {0x00};
    auto readn = RequestResponse(SlaveId, cmd, buf);
    if (readn < 4) { //fixme 4
        throw TSerialDeviceTransientErrorException("mercury200: read frame too short for command response");
    }
    if (IsBadHeader(SlaveId, cmd, buf)) {
        throw TSerialDeviceTransientErrorException("mercury200: bad response header for command");
    }
    if (IsCrcValid(buf, readn)) {
        throw TSerialDeviceTransientErrorException("mercury200: bad CRC for command");
    }
    uint8_t* payload = buf + HEADER_SZ;
    std::vector<uint8_t> result = {0};
    result.assign(payload, payload + readn - HEADER_SZ);
}


std::vector<uint8_t> TMercury200Device::ReadMemoryBlock(const PMemoryBlock & mb)
{
    uint8_t cmd = (mb->Address & 0xFF);

    return ExecCommand(cmd);
}

void TMercury200Device::WriteMemoryBlock(const PMemoryBlock &, const std::vector<uint8_t> &)
{
    throw TSerialDeviceException("mercury200: register writing is not supported");
}

void TMercury200Device::EndPollCycle()
{
    TSerialDevice::EndPollCycle();
}

bool TMercury200Device::IsCrcValid(uint8_t* buf, int sz) const
{
    auto actual_crc = CRC16::CalculateCRC16(buf, static_cast<uint16_t >(sz));
    uint16_t sent_crc = ((uint16_t) buf[sz] << 8) | ((uint16_t) buf[sz + 1]);
    return actual_crc != sent_crc;
}


int TMercury200Device::RequestResponse(uint32_t slave, uint8_t cmd, uint8_t* response) const
{
    using namespace std::chrono;

    uint8_t request[REQUEST_LEN];
    FillCommand(request, slave, cmd);
    Port()->WriteBytes(request, REQUEST_LEN);
    return Port()->ReadFrame(response, RESPONSE_BUF_LEN, this->DeviceConfig()->FrameTimeout);
}

void TMercury200Device::FillCommand(uint8_t* buf, uint32_t id, uint8_t cmd) const
{
    buf[0] = static_cast<uint8_t>(id >> 24);
    buf[1] = static_cast<uint8_t>(id >> 16);
    buf[2] = static_cast<uint8_t>(id >> 8);
    buf[3] = static_cast<uint8_t>(id);
    buf[4] = cmd;
    auto crc = CRC16::CalculateCRC16(buf, 5);
    buf[5] = static_cast<uint8_t>(crc >> 8);
    buf[6] = static_cast<uint8_t>(crc);
}

bool TMercury200Device::IsBadHeader(uint32_t slave_expected, uint8_t cmd_expected, uint8_t* response) const
{
    uint32_t actual_slave = ((uint32_t) response[0] << 24)
                            | ((uint32_t) response[1] << 16)
                            | ((uint32_t) response[2] << 8)
                            | (uint32_t) response[3];
    if (actual_slave != slave_expected) {
        return true;
    }
    return response[4] != cmd_expected;
}
