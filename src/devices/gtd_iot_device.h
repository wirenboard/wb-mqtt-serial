#pragma once

#include "modbus_base.h"
#include "running_average.h"
#include "serial_config.h"

class TGtdIotDevice: public TSerialDevice, public TUInt32SlaveId
{
public:
    // Low byte of key code is the mask of pressed keys
    static constexpr size_t MAX_KEYS = 8;

private:
    struct TKey
    {
        //! A press is in progress and if it is long
        bool Pressed = false;
        bool Long = false;

        uint16_t Status = 0;
        uint16_t SinglePresses = 0;
        uint16_t LongPresses = 0;
    };

    Modbus::TModbusRTUTraits ModbusTraits;
    TRunningAverage<std::chrono::microseconds, 10> ResponseTime;

    //! Value of the register polled in current register range and the error of its reading
    std::optional<uint16_t> ReadCache;
    bool ReadAttempted = false;
    std::string ReadErrorMessage;

    std::array<TKey, MAX_KEYS> Keys;

    //! Key code read before a failed read of key statuses
    uint16_t PendingKeyCode = 0;

    std::vector<uint8_t> ExecTransaction(TPort& port, const std::vector<uint8_t>& requestPdu, size_t responsePduSize);
    uint16_t ReadValue(TPort& port, uint16_t address);
    uint16_t ReadKeys(TPort& port);

public:
    TGtdIotDevice(PDeviceConfig config, PProtocol protocol);

    static void Register(TSerialDeviceFactory& factory);

    PRegisterRange CreateRegisterRange() const override;
    void ReadRegisterRange(TPort& port, PRegisterRange range, bool breakOnError = false) override;

protected:
    TRegisterValue ReadRegisterImpl(TPort& port, const TRegisterConfig& reg) override;
    void WriteRegisterImpl(TPort& port, const TRegisterConfig& reg, const TRegisterValue& regValue) override;
};
