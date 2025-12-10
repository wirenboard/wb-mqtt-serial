#pragma once

#include "serial_port.h"
#include "tcp_port.h"

class TFeaturePort: public TPort
{
public:
    TFeaturePort(PPort basePort, bool modbusTcp, bool connectedToMge = false);

    // TPort interface

    void Open() override;
    void Close() override;
    void Reopen() override;
    bool IsOpen() const override;
    void CheckPortOpen() const override;
    void WriteBytes(const uint8_t* buf, int count) override;
    uint8_t ReadByte(const std::chrono::microseconds& timeout) override;
    TReadFrameResult ReadFrame(uint8_t* buf,
                               size_t count,
                               const std::chrono::microseconds& responseTimeout,
                               const std::chrono::microseconds& frameTimeout,
                               TFrameCompletePred frame_complete = 0) override;
    void SkipNoise() override;
    void SleepSinceLastInteraction(const std::chrono::microseconds& us) override;
    std::chrono::microseconds GetSendTimeBytes(double bytesNumber) const override;
    std::chrono::microseconds GetSendTimeBits(size_t bitsNumber) const override;
    std::string GetDescription(bool verbose = true) const override;
    void ApplySerialPortSettings(const TSerialPortConnectionSettings& settings) override;
    void ResetSerialPortSettings() override;

    // Additional methods

    PPort GetBasePort() const;

    bool IsModbusTcp() const;

    /**
     * @brief Returns true if the underlying port supports fast Modbus.
     *        Correct value can be returned only if the port is open.
     *        For TCP ports Fast Modbus is supported only with WB-MGE v.3.
     *        It is detected automatically on port open.
     */
    bool SupportsFastModbus() const;

private:
    PPort BasePort;
    bool ModbusTcp;
    bool ConnectedToMge;
    bool FastModbus;
};

using PFeaturePort = std::shared_ptr<TFeaturePort>;
