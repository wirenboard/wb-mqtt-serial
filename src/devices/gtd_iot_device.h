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

    std::array<TKey, MAX_KEYS> Keys;

    //! Key code read before a failed read of key statuses
    uint16_t PendingKeyCode = 0;

    std::vector<uint8_t> ExecTransaction(TPort& port, const std::vector<uint8_t>& requestPdu, size_t responsePduSize);
    uint16_t ReadValue(TPort& port, uint16_t address);
    void ReadKeys(TPort& port);
    TRegisterValue GetRegisterValue(const TRegisterConfig& reg, uint16_t value) const;

    //! newPress is set if the key code has the bit of the key
    static void UpdateKey(TKey& key, bool newPress, uint16_t status);

    //! Number of keys to poll: the configured key with the highest address defines it
    size_t GetKeyCount() const;

public:
    TGtdIotDevice(PDeviceConfig config, PProtocol protocol);

    static void Register(TSerialDeviceFactory& factory);

    PRegisterRange CreateRegisterRange() const override;
    void ReadRegisterRange(TPort& port, PRegisterRange range, bool breakOnError = false) override;

protected:
    void WriteRegisterImpl(TPort& port, const TRegisterConfig& reg, const TRegisterValue& regValue) override;
};
