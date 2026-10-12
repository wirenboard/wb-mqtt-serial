#pragma once

#include <map>
#include <mutex>
#include <set>
#include <tuple>
#include <vector>

#include "port/port.h"

//! A port with Modbus RTU devices answering from their holding registers, the unset ones are 0. Thread-safe
class TModbusSimulatorPort: public TPort
{
public:
    //! slave id, address, value
    typedef std::tuple<uint8_t, uint16_t, uint16_t> TWrite;

    explicit TModbusSimulatorPort(const std::string& path);

    //! The device answers the requests
    void AddDevice(uint8_t slaveId);
    void SetRegister(uint8_t slaveId, uint16_t address, uint16_t value);
    //! Each char is in the low byte of a register, as Wiren Board devices keep strings
    void SetString(uint8_t slaveId, uint16_t address, const std::string& value);
    //! The device answers "illegal data address" for the register
    void SetUnsupported(uint8_t slaveId, uint16_t address);
    //! The device does not answer the requests of the registers from the address on
    void SetSilentFrom(uint8_t slaveId, uint16_t address);
    void SetAllowOpen(bool allowOpen);

    std::vector<TWrite> GetWrites();
    void ClearWrites();
    size_t GetCloseCount();
    //! The baud rates given to ApplySerialPortSettings
    std::vector<int> GetAppliedBaudRates();

    void Open() override;
    void Close() override;
    bool IsOpen() const override;
    void CheckPortOpen() const override;
    void WriteBytes(const uint8_t* buf, int count) override;
    uint8_t ReadByte(const std::chrono::microseconds& timeout) override;
    TReadFrameResult ReadFrame(uint8_t* buf,
                               size_t count,
                               const std::chrono::microseconds& responseTimeout,
                               const std::chrono::microseconds& frameTimeout,
                               TFrameCompletePred frameComplete = 0) override;
    void SkipNoise() override;
    void SleepSinceLastInteraction(const std::chrono::microseconds& us) override;
    std::chrono::microseconds GetSendTimeBytes(double bytesNumber) const override;
    std::chrono::microseconds GetSendTimeBits(size_t bitsNumber) const override;
    std::string GetDescription(bool verbose = true) const override;
    void ApplySerialPortSettings(const TSerialPortConnectionSettings& settings) override;

private:
    void MakeResponse(const std::vector<uint8_t>& request);
    void SetResponse(std::vector<uint8_t> response);

    std::string Path;
    mutable std::mutex Mutex;
    bool AllowOpen = true;
    bool PortIsOpen = false;
    size_t CloseCount = 0;
    std::set<uint8_t> Devices;
    std::map<std::pair<uint8_t, uint16_t>, uint16_t> Registers;
    std::set<std::pair<uint8_t, uint16_t>> Unsupported;
    std::map<uint8_t, uint16_t> SilentFrom;
    std::vector<TWrite> Writes;
    std::vector<int> AppliedBaudRates;
    std::vector<uint8_t> Response;
};

typedef std::shared_ptr<TModbusSimulatorPort> PModbusSimulatorPort;
