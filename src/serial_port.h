#pragma once
#include <chrono>
#include <termios.h>

#include "file_descriptor_port.h"
#include "serial_port_settings.h"


class TSerialPort: public TFileDescriptorPort
{
    using Base = TFileDescriptorPort;
public:
    TSerialPort(const PSerialPortSettings & settings);
    ~TSerialPort() = default;

    void Open() override;
    void Close() override;

    void WriteBytes(const uint8_t* buf, int count) override;

    uint8_t ReadByte(const std::chrono::microseconds& timeout) override;
    int ReadFrame(uint8_t* buf,
                  int count,
                  const std::chrono::microseconds& responseTimeout,
                  const std::chrono::microseconds& frameTimeout,
                  TFrameCompletePred frameComplete = 0) override;

    std::chrono::milliseconds GetSendTime(double bytesNumber) override;

private:
    PSerialPortSettings Settings;
    termios             OldTermios;
};

using PSerialPort = std::shared_ptr<TSerialPort>;
