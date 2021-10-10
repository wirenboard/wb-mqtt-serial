#pragma once

#include "definitions.h"

#include <chrono>
#include <functional>
#include <vector>
#include <string>

#include "serial_port_settings.h"

class TPort: public std::enable_shared_from_this<TPort>
{
public:
    using TFrameCompletePred = std::function<bool(uint8_t* buf, int size)>;

    TPort() = default;
    TPort(const TPort&) = delete;
    TPort& operator=(const TPort&) = delete;
    virtual ~TPort() = default;

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
     *        Throws TSerialDeviceTransientErrorException if nothing received during timeout.
     *        Throws TSerialDeviceException on internal errors.
     *
     * @param buf receiving buffer for frame
     * @param count maximum bytes to receive
     * @param responseTimeout maximum waiting timeout before first byte of frame
     * @param frameTimeout minimum inter-frame delay
     * @param frame_complete
     * @return size_t received byte count
     */
    virtual size_t ReadFrame(uint8_t* buf,
                             size_t count,
                             const std::chrono::microseconds& responseTimeout,
                             const std::chrono::microseconds& frameTimeout,
                             TFrameCompletePred frame_complete = 0) = 0;

    virtual void SkipNoise() = 0;

    virtual void SleepSinceLastInteraction(const std::chrono::microseconds& us) = 0;
    virtual bool Wait(const PBinarySemaphore& semaphore, const TTimePoint& until) = 0;
    virtual TTimePoint CurrentTime() const = 0;

    /**
     * @brief Calculate sending time for bytesNumber bytes
     *
     * @param bytesCount number of bytes
     */
    virtual std::chrono::milliseconds GetSendTime(double bytesNumber);

    virtual std::string GetDescription(bool verbose = true) const = 0;

    /**
     * @brief Set new byte parameters if it is a serial port.
     *
     * @param params pointer to new parameters, if nullptr the port will use default values set on startup
     */
    virtual void SetSerialPortByteFormat(const TSerialPortByteFormat* params);
};

using PPort = std::shared_ptr<TPort>;

class TPortOpenCloseLogic
{
public:
    struct TSettings
    {
        std::chrono::milliseconds MaxFailTime = std::chrono::milliseconds(5000);
        int ConnectionMaxFailCycles = 2;
        std::chrono::milliseconds ReopenTimeout = std::chrono::milliseconds(5000);
    };

    TPortOpenCloseLogic(const TPortOpenCloseLogic::TSettings& settings);

    void OpenIfAllowed(PPort port);
    void CloseIfNeeded(PPort port, bool allPreviousDataExchangeWasFailed);

private:
    TPortOpenCloseLogic::TSettings Settings;
    std::chrono::steady_clock::time_point LastSuccessfulCycle;
    size_t RemainingFailCycles;
    std::chrono::steady_clock::time_point NextOpenTryTime;
};
