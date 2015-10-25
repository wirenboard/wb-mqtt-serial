#include "mercury230_protocol.h"
#include "crc16.h"

TMercury230Protocol::TMercury230Protocol(PAbstractSerialPort port)
    : TEMProtocol(port) {}

bool TMercury230Protocol::ConnectionSetup(uint8_t slave)
{
    static uint8_t setupCmd[] = {
        ACCESS_LEVEL, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01
    };

    uint8_t buf[1];
    WriteCommand(slave, 0x01, setupCmd, 7);
    try {
        ReadResponse(slave, 0x00, buf, 0);
        return true;
    } catch (TSerialProtocolTransientErrorException&) {
            // retry upon response from a wrong slave
        return false;
    }
}

const TMercury230Protocol::TValueArray& TMercury230Protocol::ReadValueArray(uint32_t slave, uint32_t address)
{
    int key = (address >> 4) || (slave << 24);
    auto it = CachedValues.find(key);
    if (it != CachedValues.end())
        return it->second;

    uint8_t cmdBuf[2];
    cmdBuf[0] = (address >> 4) & 0xff; // high nibble = array number, lower nibble = month
    cmdBuf[1] = (address >> 12) & 0x0f; // tariff
    uint8_t buf[MAX_LEN], *p = buf;
    TValueArray a;
    Talk(slave, 0x05, cmdBuf, 2, -1, buf, 16);
    for (int i = 0; i < 4; i++, p += 4) {
        a.values[i] = ((uint32_t)p[1] << 24) +
                      ((uint32_t)p[0] << 16) +
                      ((uint32_t)p[3] << 8 ) +
                       (uint32_t)p[2];
    }

    return CachedValues.insert(std::make_pair(key, a)).first->second;
}

uint32_t TMercury230Protocol::ReadParam(uint32_t slave, uint32_t address)
{
    uint8_t cmdBuf[2];
    cmdBuf[0] = (address >> 8) & 0xff; // param
    cmdBuf[1] = address & 0xff; // subparam (BWRI)
    uint8_t subparam = (address & 0xff) >> 4;
    bool isPowerOrPowerCoef = subparam == 0x00 || subparam == 0x03;
    uint8_t buf[3];
    Talk(slave, 0x08, cmdBuf, 2, -1, buf, 3);
    return (((uint32_t)buf[0] << 16) & (isPowerOrPowerCoef ? 0x3f : 0xff)) +
            ((uint32_t)buf[2] << 8) +
             (uint32_t)buf[1];
}

uint64_t TMercury230Protocol::ReadRegister(uint32_t slave, uint32_t address, RegisterFormat)
{
    EnsureSlaveConnected(slave);

    uint8_t opcode = address >> 16;
    switch (opcode) {
    case 0x05:
        if (((opcode >> 8) & 0x0f) > 5)
            throw TSerialProtocolException("mercury230: unsupported array index");

        return ReadValueArray(slave, address).values[address & 0x03];
    case 0x08:
        return ReadParam(slave, address & 0xffff);
    default:
        throw TSerialProtocolException("mercury230: read opcodes other than 0x05 not supported");
    }
}

void TMercury230Protocol::EndPollCycle()
{
    CachedValues.clear();
}

// TBD: custom password?
// TBD: reconnection (session setup)
// TBD: settings in uniel template: 9600 8N1, timeout ms = 1000
