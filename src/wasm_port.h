#include "port.h"

class TWASMPort: public TPort
{
    std::vector<uint8_t> LastRequest;

public:
    TWASMPort()
    {}

    void Open() override
    {}

    void Close() override
    {}

    bool IsOpen() const override
    {
        return true;
    }

    void CheckPortOpen() const override
    {}

    void WriteBytes(const uint8_t* buffer, int count) override;

    uint8_t ReadByte(const std::chrono::microseconds& timeout) override
    {
        return 0;
    }

    TReadFrameResult ReadFrame(uint8_t* buffer,
                               size_t count,
                               const std::chrono::microseconds& responseTimeout,
                               const std::chrono::microseconds& frameTimeout,
                               TFrameCompletePred frame_complete = 0) override;

    void SkipNoise() override
    {}

    void SleepSinceLastInteraction(const std::chrono::microseconds& us) override
    {}

    std::string GetDescription(bool verbose) const override
    {
        return std::string();
    }

    const std::vector<uint8_t>& GetLastRequest() const
    {
        return LastRequest;
    }

    std::chrono::microseconds GetSendTimeBytes(double bytesNumber) const override
    {
        // 8-N-2
        auto bits = std::ceil(11 * bytesNumber);
        return GetSendTimeBits(bits);
    }

    std::chrono::microseconds GetSendTimeBits(size_t bitsNumber) const override
    {
        // 115200 bps
        auto us = std::ceil((1000000.0 * bitsNumber) / 115200.0);
        return std::chrono::microseconds(static_cast<std::chrono::microseconds::rep>(us));
    }
};
