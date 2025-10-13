#include "port.h"

class TWASMPort: public TPort
{
public:
    TWASMPort();

    void Open() override;
    void Close() override;
    bool IsOpen() const override;
    void CheckPortOpen() const override;
    void WriteBytes(const uint8_t* buffer, int count) override;
    uint8_t ReadByte(const std::chrono::microseconds& timeout) override;

    TReadFrameResult ReadFrame(uint8_t* buffer,
                               size_t count,
                               const std::chrono::microseconds& responseTimeout,
                               const std::chrono::microseconds& frameTimeout,
                               TFrameCompletePred frame_complete = 0) override;

    void SkipNoise() override;
    void SleepSinceLastInteraction(const std::chrono::microseconds& us) override;
    std::chrono::microseconds GetSendTimeBytes(double bytesNumber) const override;
    std::chrono::microseconds GetSendTimeBits(size_t bitsNumber) const override;
    std::string GetDescription(bool verbose) const override;
    void ApplySerialPortSettings(const TSerialPortConnectionSettings& settings) override;
};
