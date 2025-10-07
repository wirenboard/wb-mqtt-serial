#pragma once

#include "file_descriptor_port.h"
#include "tcp_port_settings.h"

class TTcpPort final: public TFileDescriptorPort
{
    using Base = TFileDescriptorPort;

public:
    TTcpPort(const TTcpPortSettings& settings);
    ~TTcpPort() = default;

    void Open() override;
    void WriteBytes(const uint8_t* buf, int count) override;
    uint8_t ReadByte(const std::chrono::microseconds& timeout) override;
    TReadFrameResult ReadFrame(uint8_t* buf,
                               size_t count,
                               const std::chrono::microseconds& responseTimeout,
                               const std::chrono::microseconds& frameTimeout,
                               TFrameCompletePred frame_complete = 0) override;

    std::string GetDescription(bool verbose = true) const override;

    std::chrono::microseconds GetSendTimeBytes(double bytesNumber) const override;
    std::chrono::microseconds GetSendTimeBits(size_t bitsNumber) const override;

private:
    void OnReadyEmptyFd() override;

    TTcpPortSettings Settings;
};
