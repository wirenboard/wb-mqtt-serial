#pragma once

#include <string>
#include <memory>
#include <exception>
#include <unordered_set>
#include <functional>
#include <cstdint>
#include <cstring>

#include "serial_device.h"
#include "crc16.h"

// Common device base for electricity meters
template<class Proto>
class TEMDevice: public TBasicProtocolSerialDevice<Proto> {
public:
    static const int DefaultTimeoutMs = 1000;

    TEMDevice(PDeviceConfig device_config, PAbstractSerialPort port, PProtocol protocol)
        : TBasicProtocolSerialDevice<Proto>(device_config, port, protocol)
    {}

    void WriteRegister(PRegister reg, uint64_t value)
    {
        throw TSerialDeviceException("EM protocol: writing to registers not supported");
    }

protected:
    enum ErrorType {
        NO_ERROR,
        NO_OPEN_SESSION,
        OTHER_ERROR
    };
    const int MAX_LEN = 64;

    virtual bool ConnectionSetup(uint8_t slave) = 0;
    virtual ErrorType CheckForException(uint8_t* frame, int len, const char** message) = 0;
    void WriteCommand(uint8_t slave, uint8_t cmd, uint8_t* payload, int len)
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
        this->Port()->WriteBytes(buf, p - buf);
    }

    bool ReadResponse(uint8_t slave, int expectedByte1, uint8_t* payload, int len,
                               TAbstractSerialPort::TFrameCompletePred frame_complete = 0)
    {
        uint8_t buf[MAX_LEN], *p = buf;
        int nread = this->Port()->ReadFrame(buf, MAX_LEN, this->DeviceConfig()->FrameTimeout, frame_complete);
        if (nread < 4)
            throw TSerialDeviceTransientErrorException("frame too short");

        uint16_t crc = CRC16::CalculateCRC16(buf, nread - 2),
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

        int actualPayloadSize = nread - (p - buf) - 2;
        if (len >= 0 && len != actualPayloadSize)
            throw TSerialDeviceTransientErrorException("unexpected frame size");
        else
            len = actualPayloadSize;

        std::memcpy(payload, p, len);
        return true;
    }
    void Talk(uint8_t slave, uint8_t cmd, uint8_t* payload, int payload_len,
              int expected_byte1, uint8_t* resp_payload, int resp_payload_len,
              TAbstractSerialPort::TFrameCompletePred frame_complete = 0)
    {
        EnsureSlaveConnected(slave);
        WriteCommand(slave, cmd, payload, payload_len);
        try {
            while (!ReadResponse(slave, expected_byte1, resp_payload, resp_payload_len, frame_complete)) {
                EnsureSlaveConnected(slave, true);
                WriteCommand(slave, cmd, payload, payload_len);
            }
        } catch (const TSerialDeviceTransientErrorException& e) {
            this->Port()->SkipNoise();
            throw;
        }
    }


private:
    void EnsureSlaveConnected(uint8_t slave, bool force = false)
    {
        if (!force && ConnectedSlaves.find(slave) != ConnectedSlaves.end())
            return;

        ConnectedSlaves.erase(slave);
        this->Port()->SkipNoise();
        if (!ConnectionSetup(slave))
            throw TSerialDeviceTransientErrorException("failed to establish meter connection");

        ConnectedSlaves.insert(slave);
    }

    std::unordered_set<uint8_t> ConnectedSlaves;
};
