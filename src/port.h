#pragma once

#include "common_utils.h"
#include "definitions.h"

#include <chrono>
#include <functional>
#include <string>
#include <vector>

#include "serial_port_settings.h"

const std::chrono::milliseconds RESPONSE_TIMEOUT_NOT_SET(-1);
const std::chrono::milliseconds DEFAULT_RESPONSE_TIMEOUT(500);

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
     * @brief Read frame.
     *        Throws TSerialDeviceException-based on internal errors.
     *        Throws TResponseTimeoutException if nothing received during timeout.
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
    virtual std::chrono::microseconds GetSendTimeBytes(double bytesNumber) const = 0;

    /**
     * @brief Calculate sending time for bitsNumber bits
     *
     * @param bitsCount number of bits
     */
    virtual std::chrono::microseconds GetSendTimeBits(size_t bitsNumber) const = 0;

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

    /**
     * @brief Set minimal response timeout for port.
     *        The value will be used as a minimum response timeout for all read operations.
     *        If both the timeout and responseTimeout in read operation are less than 0,
     *        DEFAULT_RESPONSE_TIMEOUT will be used.
     *
     * @param value new minimal response timeout
     */
    void SetMinimalResponseTimeout(const std::chrono::microseconds& value);

    const std::chrono::microseconds& GetMinimalResponseTimeout() const;

    /**
     * @brief Calculates the actual response timeout based on the requested timeout.
     *        If the requested timeout is less than the minimal response timeout,
     *        the minimal response timeout will be used.
     *        If both requested timeout and minimal response timeout are less than 0,
     *        DEFAULT_RESPONSE_TIMEOUT will be used.
     *
     * @param requestedTimeout The timeout duration requested, in microseconds.
     * @return The calculated response timeout, in microseconds.
     */
    std::chrono::microseconds CalcResponseTimeout(const std::chrono::microseconds& requestedTimeout) const;

private:
    std::chrono::microseconds MinimalResponseTimeout = RESPONSE_TIMEOUT_NOT_SET;
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
