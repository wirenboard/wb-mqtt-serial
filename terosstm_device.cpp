#include "terosstm_device.h"
#include "utils.h"

#include <wbmqtt/utils.h>

using Utils::WriteBCD;
using Utils::WriteHex;
using Utils::ReadHex;
using Utils::ReadBCD;

namespace {
    const int FrameTimeout = 1000;
};
    
REGISTER_AND_MAKE_PROTOCOL(TTerossTMProtocol, "teross-tm", TRegisterTypes({
    { TTerossTMDevice::REG_DEFAULT, "default", "value", Double, true },
    { TTerossTMDevice::REG_STATE, "state", "value", U64, true },
    { TTerossTMDevice::REG_VERSION, "version", "value", U16, true }  // version format: from "AB.CD" => ( (AB << 8) + CD) (dec))
                                                                     // so VER_MAJOR = (ver >> 8) & 0xFF
                                                                     //    VER_MINOR = ver & 0xFF
}));
    
template<>
TTerossTMSlaveId TBasicProtocolConverter<TTerossTMSlaveId>::ConvertSlaveId(const std::string &s) const
{
    uint32_t device_id;
    uint8_t circuit_id;

    auto strs = StringSplit(s, ':');
    if (strs.size() != 2) {
        throw TSerialDeviceException("wrong address field for Teross-TM; should be [dev_id]:[circuit_id], but \"" + s + "\" given");
    }

    device_id = std::stoi(strs[0], 0, 0);
    circuit_id = std::stoi(strs[1], 0, 0);

    return TTerossTMSlaveId(device_id, circuit_id);
}

template<>
std::string TBasicProtocolConverter<TTerossTMSlaveId>::SlaveIdToString(const TTerossTMSlaveId &s) const
{
    return std::to_string(s.DeviceId) + ":" + std::to_string(s.CircuitId);
}



TTerossTMDevice::TTerossTMDevice(PDeviceConfig device_config, PAbstractSerialPort port, PProtocol protocol)
    : TBasicProtocolSerialDevice<TTerossTMProtocol>(device_config, port, protocol)
{}

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
    WriteRequest(reg->Type, reg->Address);

    uint8_t buffer[32];
    size_t reply_size = ReceiveReply(buffer, sizeof (buffer));

    switch (reg->Type)
    {
    case TTerossTMDevice::REG_DEFAULT:
        return ReadDataRegister(buffer, reply_size);
    case TTerossTMDevice::REG_STATE:
        return ReadStateRegister(buffer, reply_size);
    case TTerossTMDevice::REG_VERSION:
        return ReadVersionRegister(buffer, reply_size);
    default:
        throw TSerialDeviceException("Teross-TM protocol: Wrong register type");
    }
}

size_t TTerossTMDevice::ReceiveReply(uint8_t *buffer, size_t max_size)
{
    size_t exp_size = 0;

    size_t nread = Port()->ReadFrame(buffer, max_size, std::chrono::milliseconds(FrameTimeout),
                        [&exp_size] (uint8_t *buf, size_t size) {
                            if (size < 4)
                                return false;
                            
                            if (exp_size == 0) {
                                if (buf[4] == 16) {
                                    if (size < 7)
                                        return false;

                                    if (buf[7] >= 0 && buf[7] <= 6)
                                        exp_size = 32; // TODO check sizes on real device
                                    else
                                        exp_size = 16;

                                } else {
                                    exp_size = 16;
                                }
                            }

                            return (size == exp_size);
                        });

    if (nread != 16 || nread != 32) {
        throw TSerialDeviceTransientErrorException("wrong frame size");
    } else {
        // test checksum
        uint16_t ts = ReadHex(buffer + nread - 2, 2);
        if (ts != CalculateChecksum(buffer, nread - 2)) {
            throw TSerialDeviceTransientErrorException("invalid checksum");
        }

        // test address
        if (ReadBCD(buffer, sizeof (uint32_t), false) != SlaveId.DeviceId)
            throw TSerialDeviceTransientErrorException("address mismatch");

        if (buffer[5] == 0) {
            CheckVersionReplyMessage(buffer, nread);
        } else if (buffer[5] == 16) {
            CheckValueReplyMessage(buffer, nread);
        } else {
            throw TSerialDeviceTransientErrorException("unsupported reply command");
        }
    }

    return exp_size;
}

void TTerossTMDevice::CheckVersionReplyMessage(uint8_t *buf, size_t size)
{
    // check point symbol on its position and model code (M or B)
    if (size != 16 || buf[7] != '.' || buf[10] != 0 || !(buf[11] == 'M' || buf[11] == 'B')) {
        throw TSerialDeviceTransientErrorException("wrong version frame format");
    }
}

void TTerossTMDevice::CheckValueReplyMessage(uint8_t *buf, size_t size)
{
    if (buf[5] != 0)
        throw TSerialDeviceTransientErrorException("unsupported parameter code");

    if (buf[6] != SlaveId.CircuitId)
        throw TSerialDeviceTransientErrorException("unexpected circuit ID");

    if (buf[7] != 50 && buf[7] > 20)
        throw TSerialDeviceTransientErrorException("unsupported parameter ID");
}

void TTerossTMDevice::WriteRequest(int type, uint8_t reg)
{
    uint8_t buffer[16] = { 0 };

    WriteBCD(SlaveId.DeviceId, &buffer[0], sizeof (uint32_t), false);

    switch (type) {
    case TTerossTMDevice::REG_DEFAULT:
        WriteDataRequest(buffer, reg);
        break;
    case TTerossTMDevice::REG_STATE:
        WriteStateRequest(buffer);
        break;
    case TTerossTMDevice::REG_VERSION:
        WriteVersionRequest(buffer);
        break;
    }

    WriteHex(CalculateChecksum(buffer, sizeof (buffer)), &buffer[14], sizeof (uint16_t));
    Port()->WriteBytes(buffer, sizeof (buffer));
}

void TTerossTMDevice::WriteVersionRequest(uint8_t *buffer)
{
    // write command code
    WriteHex(0, &buffer[4], sizeof (uint8_t), false);

    // nothing else required, calculate checksum and send message
    WriteHex(CalculateChecksum(buffer, sizeof (buffer)), &buffer[14], true);

    Port()->WriteBytes(buffer, sizeof (buffer));
}

void TTerossTMDevice::WriteStateRequest(uint8_t *buffer)
{
    // command code is 16
    WriteHex(16, &buffer[4], sizeof (uint8_t), false);

    // param code is 0
    WriteHex(0, &buffer[5], sizeof (uint8_t), false);

    // circuit id
    WriteHex(SlaveId.CircuitId, &buffer[6], sizeof (uint8_t), false);

    // param type is 50
    WriteHex(50, &buffer[7], sizeof (uint8_t), false);

}

void TTerossTMDevice::WriteDataRequest(uint8_t *buffer, uint8_t param_id)
{
    // command code is 16
    WriteHex(16, &buffer[4], sizeof (uint8_t), false);

    // param code is 0 (current parameters)
    WriteHex(0, &buffer[5], sizeof (uint8_t), false);

    // circuit id
    WriteHex(SlaveId.CircuitId, &buffer[6], sizeof (uint8_t), false);

    // param id is given
    WriteHex(param_id, &buffer[7], sizeof (uint8_t), false);
}

uint64_t TTerossTMDevice::ReadDataRegister(uint8_t *msg, size_t size)
{
    uint64_t result = 0;

    // TODO TBD: complex registers: sum or give as is?
    // TODO check endianess!

    if (msg[7] <= 6) { // complex registers
        union {
            float f;
            uint32_t u;
        } prev, current;

        // read prev then current value of integrator
        prev.u = ReadHex(msg + 8, sizeof (float), false); // TODO endianess
        current.u = ReadHex(msg + 12, sizeof (float), false); // TODO endianess

        prev.f += current.f;

        result = prev.u;
    } else {
        result = ReadHex(msg + 8, sizeof (float), false); // TODO endianess
    }

    return result;
}

uint64_t TTerossTMDevice::ReadStateRegister(uint8_t *msg, size_t size)
{
    uint64_t result = 0;

    for (int i = 8; i < 8 + 6; i++) {
        result <<= 8;
        result |= msg[i];
    }
    
    return result;
}

uint64_t TTerossTMDevice::ReadVersionRegister(uint8_t *msg, size_t size)
{
    for (int i = 5; i < 10; i++)
        if (i != 7)
            msg[i] -= '0';

    uint8_t major = msg[5] * 10 + msg[6];
    uint8_t minor = msg[8] * 10 + msg[9];

    return (major << 8) | minor;
}

void TTerossTMDevice::WriteRegister(PRegister reg, uint64_t value)
{
    throw TSerialDeviceException("Teross-TM protocol: write is not supported");
}
