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
    size_t  ReadFrame(uint8_t*                         buf,
                      size_t                           count,
                      const std::chrono::microseconds& responseTimeout,
                      const std::chrono::microseconds& frameTimeout,
                      TFrameCompletePred               frameComplete = 0) override;

    std::chrono::milliseconds GetSendTime(double bytesNumber) override;

    std::string GetDescription(bool verbose = true) const override;

    const TSerialPortSettings& GetSettings() const;

private:
    TSerialPortSettings Settings;
    termios OldTermios;
};

using PSerialPort = std::shared_ptr<TSerialPort>;

class TSerialPortWithIECHack: public TPort
{
    PSerialPort Port;

public:
    TSerialPortWithIECHack(PSerialPort port);
    ~TSerialPortWithIECHack() = default;

    void Open() override;
    void Close() override;
    void Reopen() override;
    bool IsOpen() const override;
    void CheckPortOpen() const override;

    void WriteBytes(const uint8_t* buf, int count) override;

    uint8_t ReadByte(const std::chrono::microseconds& timeout) override;

    size_t ReadFrame(uint8_t* buf,
                     size_t count,
                     const std::chrono::microseconds& responseTimeout,
                     const std::chrono::microseconds& frameTimeout,
                     TFrameCompletePred               frame_complete = 0) override;

    void SkipNoise() override;

    void SleepSinceLastInteraction(const std::chrono::microseconds& us) override;
    bool Wait(const PBinarySemaphore& semaphore, const TTimePoint& until) override;
    TTimePoint CurrentTime() const override;

    std::chrono::milliseconds GetSendTime(double bytesNumber) override;

    std::string GetDescription(bool verbose = true) const override;

    void SetSerialPortByteFormat(const TSerialPortByteFormat* params) override;

private:
    //! Use 7E to 8N conversion. The workaround allows using IEC devices and other devices on the same bus.
    bool UseIECHack;
};
