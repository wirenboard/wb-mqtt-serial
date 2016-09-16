#include "teross-tm.h"
#include "utils.h"

using Utils::WriteBCD;
using Utils::WriteHex;


REGISTER_PROTOCOL("teross-tm", TTerossTMDevice, TRegisterTypes({
    { TTerossTMDevice::REG_DEFAULT, "default", "value", Double, true },
    { TTerossTMDevice::REG_STATE, "state", "value", U64, true },
    { TTerossTMDevice::REG_VERSION, "version", "value", U16, true }  // version format: from "AB.CD" => ABCD (dec)
}));

uint16_t TTerossTMDevice::CalculateChecksum(const uint8_t *val, int size)
{
    uint8_t sum = 0;
    for (int i = 0; i < size - 2; i++) {
        sum += val[i];
    }

    return ((sum & 1) << 8) | sum;
}

uint64_t TTerossTMDevice::ReadRegister(PRegister reg)
{
    switch (reg->Type)
    {
    case TTerossTMDevice::REG_DEFAULT:
        return ReadDataRegister(reg);
    case TTerossTMDevice::REG_STATE:
        return ReadStateRegister(reg);
    case TTerossTMDevice::REG_VERSION:
        return ReadVersionRegister(reg);
    default:
        throw TSerialDeviceException("Teross-TM protocol: Wrong register type");
    }
}

void TTerossTMDevice::WriteRequest(uint32_t address, uint8_t channel_id, TTerossTMDevice::TRegisterType type, uint8_t reg)
{
    uint8_t buffer[16] = { 0 };

    WriteBCD(address, &buffer[0], sizeof (uint32_t), false);

    switch (type) {
    case TTerossTMDevice::REG_DEFAULT:
        WriteDataRequest(buffer, address, channel_id, reg);
        break;

    }

    WriteHex(CalculateChecksum(buffer, sizeof (buffer)), &buffer[14], true);
    Port()->WriteBytes(buffer, sizeof (buffer);
}

void TTerossTMDevice::WriteVersionRequest(uint32_t address)
{
    uint8_t buffer[16] = { 0 };

    // write slave address
    WriteBCD(address, &buffer[0], sizeof (uint32_t), false);
    
    // write command code
    WriteHex(0, &buffer[4], sizeof (uint8_t), false);

    // nothing else required, calculate checksum and send message
    WriteHex(CalculateChecksum(buffer, sizeof (buffer)), &buffer[14], true);

    Port()->WriteBytes(buffer, sizeof (buffer));
}

void TTerossTMDevice::WriteStateRequest(uint32_t address, uint8_t channel)
{
    uint8_t buffer[16] = { 0 };

    WriteBCD(address, &buffer[0], sizeof (uint32_t), false);

    // command code is 16
    WriteHex(16, &buffer[4], sizeof (uint8_t), false);

    // param code is 0
    WriteHex(0, &buffer[5], sizeof (uint8_t), false);

    // channel id
    WriteHex(channel, &buffer[6], sizeof (uint8_t), false);

    // param type is 50
    WriteHex(50, &buffer[7], sizeof (uint8_t), false);

}

void TTerossTMDevice::WriteRegister(PRegister reg, uint64_t value)
{
    throw TSerialDeviceException("Teross-TM protocol: write is not supported");
}
