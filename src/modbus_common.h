#pragma once

#include <map>

#include "modbus_base.h"
#include "serial_device.h"

namespace Modbus // modbus protocol common utilities
{
    const int MAX_READ_BITS = 2000;
    const int MAX_READ_REGISTERS = 125;
    const int MAX_WRITE_REGISTERS = 123;
    const int MAX_HOLE_CONTINUOUS_16_BIT_REGISTERS = 10;
    const int MAX_HOLE_CONTINUOUS_1_BIT_REGISTERS = MAX_HOLE_CONTINUOUS_16_BIT_REGISTERS * 8;

    enum RegisterType
    {
        REG_HOLDING = 0, // used for 'setup' regsb
        REG_INPUT,
        REG_COIL,
        REG_DISCRETE,
        REG_HOLDING_SINGLE,
        REG_HOLDING_MULTI,
    };

    /**
     * Cache for Modbus holding registers data.
     *
     * The map key is the register address.
     * Value byte order corresponds to the register "byte_order" setting.
     */
    typedef std::map<uint16_t, uint16_t> TRegisterCache;

    class TModbusRegisterRange: public TRegisterRange
    {
    public:
        TModbusRegisterRange(std::chrono::microseconds averageResponseTime);
        ~TModbusRegisterRange();

        bool Add(TPort& port, PRegister reg, std::chrono::milliseconds pollLimit) override;

        uint32_t GetStart() const;

        /**
         * @return The count of Modbus registers in the range.
         */
        size_t GetCount() const;
        uint8_t* GetBits();
        bool HasHoles() const;
        const std::string& TypeName() const;
        int Type() const;
        PSerialDevice Device() const;

        /**
         * Reads selected ragister range including holes.
         *
         * Throws TSerialDevicePermanentRegisterException if Modbus "Illegal" exception code (1/2/3) received.
         * Throws TSerialDeviceTransientErrorException on other errors.
         *
         * All exceptions are inherited from TSerialDeviceException.
         */
        void ReadRange(IModbusTraits& traits, TPort& port, uint8_t slaveId, int shift, Modbus::TRegisterCache& cache);

        std::chrono::microseconds GetResponseTime() const;

    private:
        bool HasHolesFlg = false;
        uint32_t Start;
        size_t Count = 0;
        uint8_t* Bits = 0;
        std::chrono::microseconds AverageResponseTime;
        std::chrono::microseconds ResponseTime;

        bool AddingRegisterIncreasesSize(bool isSingleBit, size_t extend) const;
        uint16_t GetQuantity() const;
    };

    PRegisterRange CreateRegisterRange(std::chrono::microseconds averageResponseTime);

    void WriteRegister(IModbusTraits& traits,
                       TPort& port,
                       uint8_t slaveId,
                       const TRegisterConfig& reg,
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

    /**
     * @brief Reads a register value from a Modbus device.
     *
     * @param traits Reference to an object implementing Modbus traits, which defines
     *               the protocol-specific behavior.
     * @param port Reference to the communication port used for Modbus communication.
     * @param slaveId The ID of the Modbus slave device to communicate with.
     * @param reg Configuration of the register to be read, including address and type.
     * @param requestDelay The delay to wait before sending the request to the device.
     * @param responseTimeout The maximum time to wait for a response from the device.
     * @param frameTimeout The maximum time to wait between frames of a Modbus response.
     * @return TRegisterValue The value read from the specified register.
     * @throws TSerialDeviceException based exception on communication errors or
     *         Modbus::TErrorBase based exception on Modbus protocol errors.
     */
    TRegisterValue ReadRegister(IModbusTraits& traits,
                                TPort& port,
                                uint8_t slaveId,
                                const TRegisterConfig& reg,
                                std::chrono::microseconds requestDelay,
                                std::chrono::milliseconds responseTimeout,
                                std::chrono::milliseconds frameTimeout);

    void WriteSetupRegisters(IModbusTraits& traits,
                             TPort& port,
                             uint8_t slaveId,
                             const TDeviceSetupItems& setupItems,
                             TRegisterCache& cache,
                             std::chrono::microseconds requestDelay,
                             std::chrono::milliseconds responseTimeout,
                             std::chrono::milliseconds frameTimeout,
                             bool breakOnError,
                             int shift = 0);

    bool EnableWbContinuousRead(PSerialDevice device,
                                IModbusTraits& traits,
                                TPort& port,
                                uint8_t slaveId,
                                TRegisterCache& cache);
} // modbus protocol common utilities
