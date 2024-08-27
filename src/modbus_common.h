#pragma once

#include <map>

#include "modbus_base.h"
#include "serial_device.h"

namespace Modbus // modbus protocol common utilities
{
    enum RegisterType
    {
        REG_HOLDING = 0, // used for 'setup' regsb
        REG_INPUT,
        REG_COIL,
        REG_DISCRETE,
        REG_HOLDING_SINGLE,
        REG_HOLDING_MULTI,
    };

    typedef std::map<int64_t, uint16_t> TRegisterCache;

    class TModbusRegisterRange: public TRegisterRange
    {
    public:
        TModbusRegisterRange(std::chrono::microseconds averageResponseTime);
        ~TModbusRegisterRange();

        bool Add(PRegister reg, std::chrono::milliseconds pollLimit) override;

        int GetStart() const;
        int GetCount() const;
        uint8_t* GetBits();
        uint16_t* GetWords();
        bool HasHoles() const;
        const std::string& TypeName() const;
        int Type() const;
        PSerialDevice Device() const;

        void ReadRange(IModbusTraits& traits, TPort& port, uint8_t slaveId, int shift, Modbus::TRegisterCache& cache);

        std::chrono::microseconds GetResponseTime() const;

    private:
        bool HasHolesFlg = false;
        uint32_t Start;
        size_t Count = 0;
        uint8_t* Bits = 0;
        uint16_t* Words = 0;
        std::chrono::microseconds AverageResponseTime;
        std::chrono::microseconds ResponseTime;

        bool AddingRegisterIncreasesSize(bool isSingleBit, size_t extend) const;
        uint16_t GetQuantity() const;
    };

    PRegisterRange CreateRegisterRange(std::chrono::microseconds averageResponseTime);

    void WriteRegister(IModbusTraits& traits,
                       TPort& port,
                       uint8_t slaveId,
                       uint32_t sn,
                       TRegister& reg,
                       const TRegisterValue& value,
                       TRegisterCache& cache,
                       std::chrono::microseconds requestDelay,
                       std::chrono::milliseconds responseTimeout,
                       std::chrono::milliseconds frameTimeout,
                       int shift = 0);

    void ReadRegisterRange(IModbusTraits& traits,
                           TPort& port,
                           uint8_t slaveId,
                           TModbusRegisterRange& range,
                           TRegisterCache& cache,
                           int shift = 0);

    void WriteSetupRegisters(IModbusTraits& traits,
                             TPort& port,
                             uint8_t slaveId,
                             uint32_t sn,
                             const std::vector<PDeviceSetupItem>& setupItems,
                             TRegisterCache& cache,
                             std::chrono::microseconds requestDelay,
                             std::chrono::milliseconds responseTimeout,
                             std::chrono::milliseconds frameTimeout,
                             int shift = 0);

    void EnableWbContinuousRead(PSerialDevice device,
                                IModbusTraits& traits,
                                TPort& port,
                                uint8_t slaveId,
                                TRegisterCache& cache);
} // modbus protocol common utilities
