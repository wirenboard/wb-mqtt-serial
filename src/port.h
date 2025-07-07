#pragma once

#include "common_utils.h"
#include "definitions.h"

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "serial_port_settings.h"

struct TReadFrameResult
{
    //! Received byte count
    size_t Count = 0;

    //! Time to first byte
    std::chrono::microseconds ResponseTime = std::chrono::microseconds::zero();
};

class TPort: public std::enable_shared_from_this<TPort>
{
public:
    using TFrameCompletePred = std::function<bool(uint8_t* buf, size_t size)>;

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
     * @brief Read frame
     *
     * @param buf receiving buffer for frame
     * @param count maximum bytes to receive
     * @param responseTimeout maximum waiting timeout before first byte of frame
     * @param frameTimeout minimum inter-frame delay
     * @param frame_complete
     *
     * @throws TResponseTimeoutException if nothing received during timeout
     * @throws TSerialDeviceException on internal errors
     */
    virtual TReadFrameResult ReadFrame(uint8_t* buf,
                                       size_t count,
                                       const std::chrono::microseconds& responseTimeout,
                                       const std::chrono::microseconds& frameTimeout,
                                       TFrameCompletePred frame_complete = 0) = 0;

    virtual void SkipNoise() = 0;

    virtual void SleepSinceLastInteraction(const std::chrono::microseconds& us) = 0;

    /**
     * @brief Calculate sending time for bytesNumber bytes
     *
     * @param bytesCount number of bytes
     */
    virtual std::chrono::microseconds GetSendTimeBytes(double bytesNumber) const;

    /**
     * @brief Calculate sending time for bitsNumber bits
     *
     * @param bitsCount number of bits
     */
    virtual std::chrono::microseconds GetSendTimeBits(size_t bitsNumber) const;

    /**
     * @brief Retrieves a description of the port. The string uniquely defines the port.
     *
     * @param verbose If true, provides a detailed description; otherwise, provides a brief description.
     * @return A string containing the description of the port.
     */
    virtual std::string GetDescription(bool verbose = true) const = 0;

    /**
     * @brief Set new connection parameters if it is a serial port
     *
     * @param settings new settings
     */
    virtual void ApplySerialPortSettings(const TSerialPortConnectionSettings& settings);

    /**
     * @brief Reset connection parameters to preconfigured if it is a serial port
     */
    virtual void ResetSerialPortSettings();
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

    TPortOpenCloseLogic(const TPortOpenCloseLogic::TSettings& settings, util::TGetNowFn nowFn);

    void OpenIfAllowed(PPort port);
    void CloseIfNeeded(PPort port, bool allPreviousDataExchangeWasFailed);

private:
    TPortOpenCloseLogic::TSettings Settings;
    std::chrono::steady_clock::time_point LastSuccessfulCycle;
    size_t RemainingFailCycles;
    std::chrono::steady_clock::time_point NextOpenTryTime;
    util::TGetNowFn NowFn;
};
