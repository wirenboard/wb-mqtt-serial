#pragma once

#include "serial_port.h"
#include "serial_config.h"
#include "register.h"

#include <bitset>
#include <array>


namespace Modbus  // modbus protocol common utilities
{
    enum RegisterType {
        REG_HOLDING = 0, // used for 'setup' regsb
        REG_INPUT,
        REG_COIL,
        REG_DISCRETE,
    };

    class TModbusRegisterRange: public TRegisterRange {
    public:
        TModbusRegisterRange(const std::list<PRegister>& regs);
        ~TModbusRegisterRange();
        void MapRange(TValueCallback value_callback, TErrorCallback error_callback);
        int GetStart() const { return Start; }
        int GetCount() const { return Count; }
        uint8_t* GetBits();
        uint16_t* GetWords();
        void SetError(bool error) { Error = error; }
        bool GetError() const { return Error; }

    private:
        bool Error = false;
        int Start, Count;
        uint8_t* Bits = 0;
        uint16_t* Words = 0;
    };

    using PModbusRegisterRange = std::shared_ptr<TModbusRegisterRange>;

    std::list<PRegisterRange> SplitRegisterList(const std::list<PRegister> reg_list, PDeviceConfig deviceConfig, bool debug);

    std::chrono::microseconds GetFrameTimeout(int baudRate);
};  // modbus protocol common utilities

namespace ModbusRTU // modbus rtu protocol utilities
{
    using TReadRequest = std::array<uint8_t, 8>;
    using TWriteRequest = std::vector<uint8_t>;

    using TReadResponse = std::vector<uint8_t>;
    using TWriteResponse = std::array<uint8_t, 8>;

    // returns predicate for frame ending detection
    TAbstractSerialPort::TFrameCompletePred ExpectNBytes(int n);

    void ComposeReadRequest(TReadRequest& req, Modbus::PModbusRegisterRange range, uint8_t slaveId);

    void ComposeWriteRequest(TWriteRequest& req, PRegister reg, uint8_t slaveId, uint64_t value);

    // parses modbus rtu response, checks crc and stores result
    void ParseReadResponse(const TReadResponse& res, Modbus::PModbusRegisterRange range);

    // checks modbus rtu response on write with crc check
    void ParseWriteResponse(const TWriteResponse& res);

    // returns number of bytes needed to hold request
    size_t InferWriteRequestSize(PRegister reg);

    // returns number of bytes needed to hold response
    size_t InferReadResponseSize(Modbus::PModbusRegisterRange range);

};  // modbus rtu protocol utilities
