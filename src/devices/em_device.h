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
class TEMDevice: public TSerialDevice, public TUInt32SlaveId
{
public:
    TEMDevice(PDeviceConfig config, PPort port, PProtocol protocol);

    void WriteRegister(PRegister reg, uint64_t value) override;

protected:
    enum ErrorType
    {
        NO_ERROR,
        NO_OPEN_SESSION,
        PERMANENT_ERROR,
        OTHER_ERROR
    };
    const int MAX_LEN = 64;

    virtual bool ConnectionSetup() = 0;

    virtual ErrorType CheckForException(uint8_t* frame, int len, const char** message) = 0;

    void WriteCommand(uint8_t cmd, uint8_t* payload, int len);

    bool ReadResponse(int expectedByte1, uint8_t* payload, int len, TPort::TFrameCompletePred frame_complete = 0);

    void Talk(uint8_t cmd,
              uint8_t* payload,
              int payload_len,
              int expected_byte1,
              uint8_t* resp_payload,
              int resp_payload_len,
              TPort::TFrameCompletePred frame_complete = 0);

    uint8_t SlaveIdWidth = 1;

private:
    void EnsureSlaveConnected(bool force = false);

    std::unordered_set<uint8_t> ConnectedSlaves;
};
