#pragma once

#include "definitions.h"

#include <chrono>
#include <functional>
#include <vector>
#include <string>


class TPort: public std::enable_shared_from_this<TPort> {
public:
    using TFrameCompletePred = std::function<bool(uint8_t* buf, int size)>;

    TPort() = default;
    TPort(const TPort &) = delete;
    TPort& operator=(const TPort &) = delete;
    virtual ~TPort() = default;

    virtual void CycleBegin();
    virtual void CycleEnd(bool ok);

    virtual void Open() = 0;
    virtual void Close() = 0;
    virtual void Reopen();
    virtual bool IsOpen() const = 0;
    virtual void CheckPortOpen() const = 0;

    virtual void WriteBytes(const uint8_t* buf, int count) = 0;
    void WriteBytes(const std::vector<uint8_t>& buf);
    void WriteBytes(const std::string& buf);

    virtual uint8_t ReadByte(const std::chrono::microseconds& timeout) = 0;

    /**
     * @brief Read frame.
     *        Throws TSerialDeviceTransientErrorExceptionБ if nothing received during timeout.
     * 
     * @param buf receiving buffer for frame
     * @param count maximum bytes to receive
     * @param responseTimeout maximum waiting timeout before first byte of frame
     * @param frameTimeout minimum inter-frame delay
     * @param frame_complete 
     * @return int received byte count
     */
    virtual int ReadFrame(uint8_t* buf, 
                          int count,
                          const std::chrono::microseconds& responseTimeout,
                          const std::chrono::microseconds& frameTimeout,
                          TFrameCompletePred frame_complete = 0) = 0;

    virtual void SkipNoise() = 0;

    virtual void SleepSinceLastInteraction(const std::chrono::microseconds& us) = 0;
    virtual bool Wait(const PBinarySemaphore & semaphore, const TTimePoint & until) = 0;
    virtual TTimePoint CurrentTime() const = 0;

    /**
     * @brief Calculate sending time for bytesNumber bytes
     * 
     * @param bytesCount number of bytes 
     */
    virtual std::chrono::milliseconds GetSendTime(double bytesNumber);
};

using PPort = std::shared_ptr<TPort>;
