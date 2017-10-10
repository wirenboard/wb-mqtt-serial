#pragma once

#include "definitions.h"

#include <chrono>
#include <functional>


class TPort: public std::enable_shared_from_this<TPort> {
public:
    using TFrameCompletePred = std::function<bool(uint8_t* buf, int size)>;

    TPort() = default;
    TPort(const TPort &) = delete;
    TPort& operator=(const TPort &) = delete;
    virtual ~TPort() = default;
    
    virtual void Open() = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;
    virtual void CheckPortOpen() const = 0;
    virtual void WriteBytes(const uint8_t * buf, int count) = 0;
    virtual uint8_t ReadByte() = 0;
    virtual int ReadFrame(
        uint8_t* buf, int count,
        const std::chrono::microseconds& timeout = std::chrono::microseconds(-1),
        TFrameCompletePred frame_complete = 0) = 0;
    virtual void SkipNoise() = 0;

    virtual void SetDebug(bool debug) = 0;
    virtual bool Debug() const = 0;
    virtual void Sleep(const std::chrono::microseconds& us) = 0;
    virtual bool Wait(const PBinarySemaphore & semaphore, const TTimePoint & until) = 0;
    virtual TTimePoint CurrentTime() const = 0;
};

using PPort = std::shared_ptr<TPort>;
