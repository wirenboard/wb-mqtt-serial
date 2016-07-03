#include <cstring>

#include "em_device.h"
#include "crc16.h"

TEMDevice::TEMDevice(PDeviceConfig device_config, PAbstractSerialPort port)
    : TSerialDevice(device_config, port), Config(device_config) {}

void TEMDevice::EnsureSlaveConnected(uint8_t slave, bool force)
{
    if (!force && ConnectedSlaves.find(slave) != ConnectedSlaves.end())
        return;

    ConnectedSlaves.erase(slave);
    Port()->SkipNoise();
    if (!ConnectionSetup(slave))
        throw TSerialDeviceTransientErrorException("failed to establish meter connection");

    ConnectedSlaves.insert(slave);
}

void TEMDevice::WriteCommand(uint8_t slave, uint8_t cmd, uint8_t* payload, int len)
{
    uint8_t buf[MAX_LEN], *p = buf;
    if (len + 4 > MAX_LEN)
        throw TSerialDeviceException("outgoing command too long");
    *p++ = slave;
    *p++ = cmd;
    while (len--)
        *p++ = *payload++;
    uint16_t crc = CRC16::CalculateCRC16(buf, p - buf);
    *p++ = crc >> 8;
    *p++ = crc & 0xff;
    Port()->WriteBytes(buf, p - buf);
}

bool TEMDevice::ReadResponse(uint8_t slave, int expectedByte1, uint8_t* payload, int len,
                               TAbstractSerialPort::TFrameCompletePred frame_complete)
{
    uint8_t buf[MAX_LEN], *p = buf;
    int nread = Port()->ReadFrame(buf, MAX_LEN, Config->FrameTimeout, frame_complete);
    if (nread < 4)
        throw TSerialDeviceTransientErrorException("frame too short");

    uint16_t crc = CRC16::CalculateCRC16(buf, static_cast<uint16_t>(nread - 2)),
        crc1 = buf[nread - 2],
        crc2 = buf[nread - 1],
        actualCrc = (crc1 << 8) + crc2;
    if (crc != actualCrc)
        throw TSerialDeviceTransientErrorException("invalid crc");

    if (*p++ != slave)
        throw TSerialDeviceTransientErrorException("invalid slave id");

    const char* msg;
    ErrorType err = CheckForException(buf, nread, &msg);
    if (err == NO_OPEN_SESSION)
        return false;
    else if (err != NO_ERROR)
        throw TSerialDeviceTransientErrorException(msg);

    if (expectedByte1 >= 0 && *p++ != expectedByte1)
        throw TSerialDeviceTransientErrorException("invalid command code in the response");

    int actualPayloadSize = static_cast<int>(nread - (p - buf) - 2);
    if (len >= 0 && len != actualPayloadSize)
        throw TSerialDeviceTransientErrorException("unexpected frame size");
    else
        len = actualPayloadSize;

    std::memcpy(payload, p, static_cast<size_t>(len));
    return true;
}

void TEMDevice::Talk(uint8_t slave, uint8_t cmd, uint8_t* payload, int payload_len,
                       int expected_byte1, uint8_t* resp_payload, int resp_payload_len,
                       TAbstractSerialPort::TFrameCompletePred frame_complete)
{
    EnsureSlaveConnected(slave);
    WriteCommand(slave, cmd, payload, payload_len);
    try {
        while (!ReadResponse(slave, expected_byte1, resp_payload, resp_payload_len, frame_complete)) {
            EnsureSlaveConnected(slave, true);
            WriteCommand(slave, cmd, payload, payload_len);
        }
    } catch (const TSerialDeviceTransientErrorException& e) {
        Port()->SkipNoise();
        throw;
    }
}

void TEMDevice::WriteRegister(PRegister, uint64_t)
{
    throw TSerialDeviceException("EM protocol: writing to registers not supported");
}

PDeviceConfig TEMDevice::DeviceConfig() const
{
    return Config;
}
