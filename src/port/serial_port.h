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

    void ApplySerialPortSettings(const TSerialPortConnectionSettings& settings) override;

    void ResetSerialPortSettings() override;

    void WriteBytes(const uint8_t* buf, int count) override;

    uint8_t ReadByte(const std::chrono::microseconds& timeout) override;
    TReadFrameResult ReadFrame(uint8_t* buf,
                               size_t count,
                               const std::chrono::microseconds& responseTimeout,
                               const std::chrono::microseconds& frameTimeout,
                               TFrameCompletePred frameComplete = 0) override;

    std::chrono::microseconds GetSendTimeBytes(double bytesNumber) const override;
    std::chrono::microseconds GetSendTimeBits(size_t bitsNumber) const override;

    std::string GetDescription(bool verbose = true) const override;

    const TSerialPortSettings& GetSettings() const;

private:
    TSerialPortSettings Settings;
    TSerialPortConnectionSettings InitialSettings;
    termios OldTermios;
    size_t RxTrigBytes;
};

using PSerialPort = std::shared_ptr<TSerialPort>;
