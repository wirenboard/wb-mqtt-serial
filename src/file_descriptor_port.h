#pragma once

#include "port.h"

/*!
 * Abstract port class for file descriptor based ports implementation
 */
class TFileDescriptorPort: public TPort
{
public:
    TFileDescriptorPort();
    ~TFileDescriptorPort();

    void WriteBytes(const uint8_t * buf, int count) override;
    uint8_t ReadByte(const std::chrono::microseconds& timeout) override;
    int ReadFrame(uint8_t * buf, int count,
                  const std::chrono::microseconds & responseTimeout,
                  const std::chrono::microseconds& frameTimeout,
                  TFrameCompletePred frame_complete = 0) override;
    void SkipNoise() override;
    void Close() override;
    void CheckPortOpen() const override;
    bool IsOpen() const override;

    void Sleep(const std::chrono::microseconds & us) override;
    void SleepSinceLastInteraction(const std::chrono::microseconds& us) override;
    bool Wait(const PBinarySemaphore & semaphore, const TTimePoint & until) override;
    TTimePoint CurrentTime() const override;

protected:
    bool Select(const std::chrono::microseconds& us);
    virtual void OnReadyEmptyFd();

    int             Fd;
    std::chrono::time_point<std::chrono::high_resolution_clock> LastInteraction;
private:
    int ReadAvailableData(uint8_t * buf, size_t max_read);
};


