#pragma once

#include "file_descriptor_port.h"
#include "tcp_port_settings.h"


class TTcpPort final: public TFileDescriptorPort
{
    using Base = TFileDescriptorPort;
public:
    TTcpPort(const PTcpPortSettings & settings);
    ~TTcpPort() = default;

    void CycleBegin() override;
    void CycleEnd(bool ok) override;
    void Open() override;
    void WriteBytes(const uint8_t * buf, int count) override;
    int ReadFrame(uint8_t * buf, int count,
                  const std::chrono::microseconds & timeout = std::chrono::microseconds(-1),
                  TFrameCompletePred frame_complete = 0) override;

private:
    void OpenTcpPort();
    void Reset() noexcept;
    void OnConnectionOk();

    void OnReadyEmptyFd() override;

    PTcpPortSettings                        Settings;
    std::chrono::steady_clock::time_point   LastSuccessfulCycle;
    int                                     RemainingFailCycles;
};
