#include "em_device.h"

TEMDevice::TEMDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : TSerialDevice(config, port, protocol),
      TUInt32SlaveId(config->SlaveId, true)
{
    if (HasBroadcastSlaveId) {
        SlaveId = 0;
    }
}

void TEMDevice::WriteRegister(PRegister reg, uint64_t value)
{
    throw TSerialDeviceException("EM protocol: writing to registers not supported");
}

void TEMDevice::WriteCommand(uint8_t cmd, uint8_t* payload, int len)
{
    uint8_t buf[MAX_LEN], *p = buf;
    if (len + 3 + SlaveIdWidth > MAX_LEN)
        throw TSerialDeviceException("outgoing command too long");

    // SlaveId is sent in reverse (little-endian) order
    for (int i = 0; i < SlaveIdWidth; ++i) {
        *p++ = (SlaveId & (0xFF << (8 * i))) >> (8 * i);
    }

    *p++ = cmd;
    while (len--)
        *p++ = *payload++;
    uint16_t crc = CRC16::CalculateCRC16(buf, p - buf);
    *p++ = crc >> 8;
    *p++ = crc & 0xff;
    Port()->WriteBytes(buf, p - buf);
}

bool TEMDevice::ReadResponse(int expectedByte1, uint8_t* payload, int len, TPort::TFrameCompletePred frame_complete)
{
    uint8_t buf[MAX_LEN], *p = buf;
    int nread =
        Port()->ReadFrame(buf, MAX_LEN, DeviceConfig()->ResponseTimeout, DeviceConfig()->FrameTimeout, frame_complete);
    if (nread < 3 + SlaveIdWidth)
        throw TSerialDeviceTransientErrorException("frame too short");

    uint16_t crc = CRC16::CalculateCRC16(buf, nread - 2), crc1 = buf[nread - 2], crc2 = buf[nread - 1],
             actualCrc = (crc1 << 8) + crc2;
    if (crc != actualCrc)
        throw TSerialDeviceTransientErrorException("invalid crc");

    for (int i = 0; i < SlaveIdWidth; ++i) {
        if (*p++ != (SlaveId & (0xFF << (8 * i))) >> (8 * i)) {
            throw TSerialDeviceTransientErrorException("invalid slave id");
        }
    }

    const char* msg;
    ErrorType err = CheckForException(buf, nread, &msg);
    if (err == NO_OPEN_SESSION)
        return false;
    if (err == PERMANENT_ERROR)
        throw TSerialDevicePermanentRegisterException(msg);
    if (err != NO_ERROR)
        throw TSerialDeviceTransientErrorException(msg);

    if (expectedByte1 >= 0 && *p++ != expectedByte1)
        throw TSerialDeviceTransientErrorException("invalid command code in the response");

    int actualPayloadSize = nread - (p - buf) - 2;
    if (len >= 0 && len != actualPayloadSize)
        throw TSerialDeviceTransientErrorException("unexpected frame size");
    else
        len = actualPayloadSize;

    std::memcpy(payload, p, len);
    return true;
}

void TEMDevice::Talk(uint8_t cmd,
                     uint8_t* payload,
                     int payload_len,
                     int expected_byte1,
                     uint8_t* resp_payload,
                     int resp_payload_len,
                     TPort::TFrameCompletePred frame_complete)
{
    EnsureSlaveConnected();
    WriteCommand(cmd, payload, payload_len);
    try {
        while (!ReadResponse(expected_byte1, resp_payload, resp_payload_len, frame_complete)) {
            EnsureSlaveConnected(true);
            WriteCommand(cmd, payload, payload_len);
        }
    } catch (const TSerialDeviceTransientErrorException& e) {
        Port()->SkipNoise();
        throw;
    }
}

void TEMDevice::EnsureSlaveConnected(bool force)
{
    if (!force && ConnectedSlaves.find(SlaveId) != ConnectedSlaves.end())
        return;

    ConnectedSlaves.erase(SlaveId);
    Port()->SkipNoise();
    if (!ConnectionSetup())
        throw TSerialDeviceTransientErrorException("failed to establish meter connection");

    ConnectedSlaves.insert(SlaveId);
}
