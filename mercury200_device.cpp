#include <cstdint>
#include <chrono>

#include "crc16.h"
#include "serial_device.h"
#include "mercury200_device.h"
#include "bcd_utils.h"

namespace
{
    const size_t RESPONSE_BUF_LEN = 100;
    const size_t REQUEST_LEN = 7;
    const int PAUSE_US = 100000; // delay of 100 ms in microseconds
    const int EXPECTED_ENERGY_SZ = 21;
    const int EXPECTED_PARAMS_SZ = 12;
    const ptrdiff_t HEADER_SZ = 5;
}

REGISTER_PROTOCOL("mercury200", TMercury200Device, TRegisterTypes(
        {
            { TMercury200Device::REG_ENERGY_VALUE, "energy", "power_consumption", BCD32, true },
            { TMercury200Device::REG_PARAM_VALUE16, "param16", "value", BCD16, true },
            { TMercury200Device::REG_PARAM_VALUE24, "param24", "value", BCD24, true }
        }));

TMercury200Device::TMercury200Device(PDeviceConfig config, PAbstractSerialPort port)
        : TSerialDevice(config, port)
{}

TMercury200Device::~TMercury200Device()
{}

const TMercury200Device::TEnergyValues& TMercury200Device::ReadEnergyValues(uint32_t slave)
{
    auto it = EnergyCache.find(slave);
    if (it != EnergyCache.end()) {
        return it->second;
    }

    uint8_t buf[RESPONSE_BUF_LEN];
    auto readn = RequestResponse(slave, 0x27, buf);
    if (readn < EXPECTED_ENERGY_SZ) {
        throw TSerialDeviceTransientErrorException("read frame too short for 0x27 command response");
    }
    if (IsBadHeader(slave, 0x27, buf)) {
        throw TSerialDeviceTransientErrorException("bad response header for 0x27 command");
    }
    if (IsCrcValid(buf, EXPECTED_ENERGY_SZ)) {
        throw TSerialDeviceTransientErrorException("bad CRC for 0x27 command");
    }
    uint8_t* payload = buf + HEADER_SZ;
    TEnergyValues a{{0, 0, 0, 0}};
    for (int i = 0; i < 4; ++i) {
        a.values[i] = PackBCD(payload + i * BCD32_SZ, BCD32_SZ);
    }
    return EnergyCache.insert({slave, a}).first->second;
}

const TMercury200Device::TParamValues& TMercury200Device::ReadParamValues(uint32_t slave)
{
    auto it = ParamCache.find(slave);
    if (it != ParamCache.end()) {
        return it->second;
    }

    uint8_t buf[RESPONSE_BUF_LEN] = {0x00};
    auto readn = RequestResponse(slave, 0x63, buf);
    if (readn < EXPECTED_PARAMS_SZ) {
        throw TSerialDeviceTransientErrorException("read frame too short for 0x63 command response");
    }
    if (IsBadHeader(slave, 0x63, buf)) {
        throw TSerialDeviceTransientErrorException("bad response header for 0x63 command");
    }
    if (IsCrcValid(buf, EXPECTED_PARAMS_SZ)) {
        throw TSerialDeviceTransientErrorException("bad CRC for 0x63 command");
    }
    uint8_t* payload = buf + HEADER_SZ;
    TParamValues a{{0, 0, 0}};
    a.values[0] = PackBCD(payload, BCD16_SZ);
    a.values[1] = PackBCD(payload + BCD16_SZ, BCD16_SZ);
    a.values[2] = PackBCD(payload + BCD32_SZ, BCD24_SZ);
    return ParamCache.insert({slave, a}).first->second;
}

uint64_t TMercury200Device::ReadRegister(PRegister reg)
{
    uint32_t slv = static_cast<uint32_t>(reg->Slave->Id);
    uint32_t adr = static_cast<uint32_t>(reg->Address) & 0x03;
    switch (reg->Type) {
    case REG_ENERGY_VALUE:
        return ReadEnergyValues(slv).values[adr];
    case REG_PARAM_VALUE16:
    case REG_PARAM_VALUE24:
        return ReadParamValues(slv).values[adr];
    default:
        throw TSerialDeviceException("mercury200.02: invalid register type");
    }
}

void TMercury200Device::WriteRegister(PRegister, uint64_t)
{
    throw TSerialDeviceException("Mercury 200 protocol: writing register is not supported");
}

void TMercury200Device::EndPollCycle()
{
    EnergyCache.clear();
    ParamCache.clear();
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
    Port()->Sleep(microseconds(PAUSE_US));
    return Port()->ReadFrame(response, RESPONSE_BUF_LEN, microseconds(PAUSE_US));
}

void TMercury200Device::FillCommand(uint8_t* buf, uint32_t id, uint8_t cmd) const
{
    buf[0] = 0x00;
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
    if (response[0] != 0x00) {
        return true;
    }
    uint32_t actual_slave = ((uint32_t) response[1] << 16)
                            | ((uint32_t) response[2] << 8)
                            | (uint32_t) response[3];
    if (actual_slave != slave_expected) {
        return true;
    }
    return response[4] != cmd_expected;
}

