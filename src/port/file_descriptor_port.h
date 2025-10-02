#pragma once

#include "port/port.h"

/*!
 * Abstract port class for file descriptor based ports implementation
 */
class TFileDescriptorPort: public TPort
{
public:
    TFileDescriptorPort();
    ~TFileDescriptorPort();

    void WriteBytes(const uint8_t* buf, int count) override;
    uint8_t ReadByte(const std::chrono::microseconds& timeout) override;
    TReadFrameResult ReadFrame(uint8_t* buf,
                               size_t count,
                               const std::chrono::microseconds& responseTimeout,
                               const std::chrono::microseconds& frameTimeout,
                               TFrameCompletePred frame_complete = 0) override;
    void SkipNoise() override;
    void Close() override;
    void CheckPortOpen() const override;
    bool IsOpen() const override;

    void SleepSinceLastInteraction(const std::chrono::microseconds& us) override;

protected:
    bool Select(const std::chrono::microseconds& us);
    virtual void OnReadyEmptyFd();

    int Fd;
    std::chrono::time_point<std::chrono::steady_clock> LastInteraction;

private:
    /**
     * @brief Reads data from port. Throws TSerialDeviceException on errors
     *
     * @param buf buffer to read to
     * @param max_read maximum bytes to read
     * @return size_t actual read bytes number
     */
    size_t ReadAvailableData(uint8_t* buf, size_t max_read);
};
