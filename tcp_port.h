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
    void Open() override;
    bool Select(const std::chrono::microseconds& us) override;
    void WriteBytes(const uint8_t * buf, int count) override;
    int ReadFrame(uint8_t * buf, int count,
                  const std::chrono::microseconds & timeout = std::chrono::microseconds(-1),
                  TFrameCompletePred frame_complete = 0) override;

private:
    void OpenTcpPort();
    void Reset() noexcept;

    void OnBadSelect();
    void OnOkSelect();

    PTcpPortSettings                        Settings;
    std::chrono::steady_clock::time_point   LastSuccessfulSelect;
};
