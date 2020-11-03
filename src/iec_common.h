#pragma once

#include <string>
#include <vector>

#include "serial_device.h"

namespace IEC
{
    const char ACK = '\x06';
    const char NAK = '\x15';
    const char ETX = '\x03';
    const char SOH = '\x01';
    const char STX = '\x02';
    const char EOT = '\x04';

    void CheckStripEvenParity(uint8_t* buf, size_t nread);

    std::vector<uint8_t> SetEvenParity(const uint8_t* buf, size_t count);

    size_t ReadFrame(TPort& port,
                     uint8_t* buf,
                     size_t count,
                     const std::chrono::microseconds& responseTimeout,
                     const std::chrono::microseconds& frameTimeout,
                     TPort::TFrameCompletePred frame_complete,
                     const std::string& logPrefix);

    void WriteBytes(TPort& port, const uint8_t* buf, size_t count, const std::string& logPrefix);
    void WriteBytes(TPort& port, const std::string& str, const std::string& logPrefix);
}

class TIECDevice: public TSerialDevice
{
public:
    TIECDevice(PDeviceConfig device_config, PPort port, PProtocol protocol);

    void EndSession() override;
    void Prepare() override;

protected:
    std::string SlaveId;
};
