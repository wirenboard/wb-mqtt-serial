#pragma once

#include "port.h"
#include "port_settings.h"

/*!
 * Abstract port class for file descriptor based ports implementation
 */
class TFileDescriptorPort: public TPort
{
public:
    TFileDescriptorPort(const PPortSettings & settings);
    ~TFileDescriptorPort();
    
    void WriteBytes(const uint8_t * buf, int count) override;
    uint8_t ReadByte() override;
    int ReadFrame(uint8_t * buf, int count,
                  const std::chrono::microseconds & timeout = std::chrono::microseconds(-1),
                  TFrameCompletePred frame_complete = 0) override;
    void SkipNoise() override;
    void Close() override;
    void CheckPortOpen() const override;
    bool IsOpen() const override;

    void Sleep(const std::chrono::microseconds & us) override;
    bool Wait(const PBinarySemaphore & semaphore, const TTimePoint & until) override;
    void SetDebug(bool debug) override;
    bool Debug() const override;
    TTimePoint CurrentTime() const override;

protected:
    bool Select(const std::chrono::microseconds& us);

    int             Fd;
    PPortSettings   Settings;
    bool            DebugEnabled;
};