#pragma once
#include <chrono>
#include <termios.h>

#include "file_descriptor_port.h"
#include "serial_port_settings.h"


class TSerialPort: public TFileDescriptorPort
{
    using Base = TFileDescriptorPort;
public:
    TSerialPort(const TSerialPortSettings& settings);
    ~TSerialPort() = default;

    void Open() override;
    void Close() override;

    void WriteBytes(const uint8_t* buf, int count) override;

    uint8_t ReadByte(const std::chrono::microseconds& timeout) override;
    size_t ReadFrame(uint8_t* buf,
                     size_t count,
                     const std::chrono::microseconds& responseTimeout,
                     const std::chrono::microseconds& frameTimeout,
                     TFrameCompletePred frameComplete = 0) override;

    std::chrono::milliseconds GetSendTime(double bytesNumber) override;

    std::string GetDescription() const override;

private:
    TSerialPortSettings Settings;
    termios             OldTermios;
};

using PSerialPort = std::shared_ptr<TSerialPort>;
