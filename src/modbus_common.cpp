#include "modbus_common.h"
#include "bin_utils.h"
#include "crc16.h"
#include "log.h"
#include "serial_device.h"

#include <array>
#include <cassert>
#include <cmath>
#include <math.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>

using namespace std;
using namespace BinUtils;

#define LOG(logger) logger.Log() << "[modbus] "

typedef uint16_t TRegisterWord;

namespace Modbus // modbus protocol declarations
{
    const int MAX_READ_BITS = 2000;

    const int MAX_READ_REGISTERS = 125;

    const size_t WRITE_RESPONSE_PDU_SIZE = 5;

    const int MAX_HOLE_CONTINUOUS_16_BIT_REGISTERS = 10;
    const int MAX_HOLE_CONTINUOUS_1_BIT_REGISTERS = MAX_HOLE_CONTINUOUS_16_BIT_REGISTERS * 8;

    const uint16_t ENABLE_CONTINUOUS_READ_REGISTER = 114;

    enum Error : uint8_t
    {
        ERR_NONE = 0x0,
        ERR_ILLEGAL_FUNCTION = 0x1,
        ERR_ILLEGAL_DATA_ADDRESS = 0x2,
        ERR_ILLEGAL_DATA_VALUE = 0x3,
        ERR_SERVER_DEVICE_FAILURE = 0x4,
        ERR_ACKNOWLEDGE = 0x5,
        ERR_SERVER_DEVICE_BUSY = 0x6,
        ERR_MEMORY_PARITY_ERROR = 0x8,
        ERR_GATEWAY_PATH_UNAVAILABLE = 0xA,
        ERR_GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND = 0xB
    };

    union TAddress
    {
        int64_t AbsAddress;
        struct
        {
            int Type;
            int Address;
        };
    };

    void ComposeReadRequestPDU(uint8_t* pdu, const TModbusRegisterRange& range, int shift);
    size_t InferReadResponsePDUSize(int type, size_t registerCount);
    // parses modbus response and stores result
    void ParseReadResponse(const uint8_t* pdu,
                           size_t pduSize,
                           TModbusRegisterRange& range,
                           Modbus::TRegisterCache& cache);
    TReadFrameResult ReadResponse(IModbusTraits& traits,
                                  TPort& port,
                                  const TRequest& request,
                                  TResponse& response,
                                  std::chrono::milliseconds responseTimeout,
                                  std::chrono::milliseconds frameTimeout);
}

namespace // general utilities
{
    inline uint32_t GetModbusDataWidthIn16BitWords(const TRegisterConfig& reg)
    {
        return reg.Get16BitWidth();
    }

    // write 16-bit value to byte array in big-endian order
    inline void WriteAs2Bytes(uint8_t* dst, uint16_t val)
    {
        dst[0] = static_cast<uint8_t>(val >> 8);
        dst[1] = static_cast<uint8_t>(val);
    }

    // returns true if multi write needs to be done
    inline bool IsPacking(const TRegisterConfig& reg)
    {
        return (reg.Type == Modbus::REG_HOLDING_MULTI) ||
               ((reg.Type == Modbus::REG_HOLDING) && (GetModbusDataWidthIn16BitWords(reg) > 1));
    }

    inline bool IsPacking(const Modbus::TModbusRegisterRange& range)
    {
        return (range.Type() == Modbus::REG_HOLDING_MULTI) ||
               ((range.Type() == Modbus::REG_HOLDING) && (range.GetCount() > 1));
    }

    inline bool IsSingleBitType(int type)
    {
        return (type == Modbus::REG_COIL) || (type == Modbus::REG_DISCRETE);
    }
} // general utilities

namespace Modbus // modbus protocol common utilities
{
    TMalformedResponseError::TMalformedResponseError(const std::string& what)
        : TSerialDeviceTransientErrorException("malformed response: " + what)
    {}

    TInvalidCRCError::TInvalidCRCError(): TMalformedResponseError("invalid crc")
    {}

    TModbusRegisterRange::TModbusRegisterRange(std::chrono::microseconds averageResponseTime)
        : AverageResponseTime(averageResponseTime),
          ResponseTime(averageResponseTime)
    {}

    TModbusRegisterRange::~TModbusRegisterRange()
    {
        if (Bits)
            delete[] Bits;
        if (Words)
            delete[] Words;
    }

    bool TModbusRegisterRange::Add(PRegister reg, std::chrono::milliseconds pollLimit)
    {
        if (reg->GetAvailable() == TRegisterAvailability::UNAVAILABLE) {
            return true;
        }

        auto& deviceConfig = *(reg->Device()->DeviceConfig());
        bool isSingleBit = IsSingleBitType(reg->Type);
        auto addr = GetUint32RegisterAddress(reg->GetAddress());
        const auto widthInWords = GetModbusDataWidthIn16BitWords(*reg);

        size_t extend;
        if (RegisterList().empty()) {
            extend = widthInWords;
        } else {
            if (HasOtherDeviceAndType(reg)) {
                return false;
            }

            // Can't add register before first register in the range
            if (Start > addr) {
                return false;
            }

            // Can't add register separated from last in the range by more than maxHole registers
            int maxHole = 0;
            if (reg->Device()->GetSupportsHoles()) {
                maxHole = isSingleBit ? deviceConfig.MaxBitHole : deviceConfig.MaxRegHole;
            }
            if (Start + Count + maxHole < addr) {
                return false;
            }

            if (RegisterList().back()->GetAvailable() == TRegisterAvailability::UNKNOWN) {
                // Read UNKNOWN 2 byte registers one by one to find availability
                if (!isSingleBit) {
                    return false;
                }
                // Disable holes for reading UNKNOWN registers
                if (Start + Count < addr) {
                    return false;
                }
                // Can read up to 16 UNKNOWN single bit registers at once
                size_t maxRegs = 16;
                if ((deviceConfig.MaxReadRegisters > 0) && (deviceConfig.MaxReadRegisters <= MAX_READ_REGISTERS)) {
                    maxRegs = deviceConfig.MaxReadRegisters;
                }
                if (reg->GetAvailable() == TRegisterAvailability::UNKNOWN && (RegisterList().size() >= maxRegs)) {
                    return false;
                }
            } else {
                // Don't mix available and unknown registers
                if (reg->GetAvailable() == TRegisterAvailability::UNKNOWN) {
                    return false;
                }

                HasHolesFlg = HasHolesFlg || (Start + Count < addr);
            }

            extend = std::max(0, static_cast<int>(addr + widthInWords) - static_cast<int>(Start + Count));

            auto maxRegs = isSingleBit ? MAX_READ_BITS : MAX_READ_REGISTERS;
            if ((deviceConfig.MaxReadRegisters > 0) && (deviceConfig.MaxReadRegisters <= maxRegs)) {
                maxRegs = deviceConfig.MaxReadRegisters;
            }
            if (Count + extend > static_cast<size_t>(maxRegs)) {
                return false;
            }
        }

        auto newPduSize = InferReadResponsePDUSize(reg->Type, Count + extend);
        // Request 8 bytes: SlaveID, Operation, Addr, Count, CRC
        // Response 5 bytes except data: SlaveID, Operation, Size, CRC
        auto sendTime = reg->Device()->Port()->GetSendTimeBytes(newPduSize + 8 + 5);
        auto newPollTime = std::chrono::ceil<std::chrono::milliseconds>(
            sendTime + AverageResponseTime + deviceConfig.RequestDelay + 2 * deviceConfig.FrameTimeout);

        if (((Count != 0) && !AddingRegisterIncreasesSize(isSingleBit, extend)) || (newPollTime <= pollLimit)) {

            if (Count == 0) {
                Start = addr;
            }

            RegisterList().push_back(reg);
            Count += extend;
            return true;
        }
        if (newPollTime > pollLimit) {
            LOG(Debug) << "Poll time for " << reg->ToString() << " is too long: " << newPollTime.count() << " ms"
                       << " (sendTime=" << sendTime.count() << " us, "
                       << "AverageResponseTime=" << AverageResponseTime.count() << " us, "
                       << "RequestDelay=" << deviceConfig.RequestDelay.count() << " ms, "
                       << "FrameTimeout=" << deviceConfig.FrameTimeout.count() << " ms)"
                       << ", limit is " << pollLimit.count() << " ms";
        }
        return false;
    }

    uint8_t* TModbusRegisterRange::GetBits()
    {
        if (!IsSingleBitType(Type()))
            throw std::runtime_error("GetBits() for non-bit register");
        if (!Bits)
            Bits = new uint8_t[Count];
        return Bits;
    }

    uint16_t* TModbusRegisterRange::GetWords()
    {
        if (IsSingleBitType(Type()))
            throw std::runtime_error("GetWords() for non-word register");
        if (!Words)
            Words = new uint16_t[Count];
        return Words;
    }

    int TModbusRegisterRange::GetStart() const
    {
        return Start;
    }

    int TModbusRegisterRange::GetCount() const
    {
        return Count;
    }

    bool TModbusRegisterRange::HasHoles() const
    {
        return HasHolesFlg;
    }

    TRequest TModbusRegisterRange::GetRequest(IModbusTraits& traits, uint8_t slaveId, int shift) const
    {
        TRequest request;
        // 1 byte - function code, 2 bytes - starting register address, 2 bytes - quantity of registers
        const uint16_t REQUEST_PDU_SIZE = 5;

        request.resize(traits.GetPacketSize(REQUEST_PDU_SIZE));
        Modbus::ComposeReadRequestPDU(traits.GetPDU(request), *this, shift);
        traits.FinalizeRequest(request, slaveId, 0);
        return request;
    }

    size_t TModbusRegisterRange::GetResponseSize(IModbusTraits& traits) const
    {
        return traits.GetPacketSize(InferReadResponsePDUSize(Type(), GetCount()));
    }

    const std::string& TModbusRegisterRange::TypeName() const
    {
        return RegisterList().front()->TypeName;
    }

    int TModbusRegisterRange::Type() const
    {
        return RegisterList().front()->Type;
    }

    PSerialDevice TModbusRegisterRange::Device() const
    {
        return RegisterList().front()->Device();
    }

    bool TModbusRegisterRange::AddingRegisterIncreasesSize(bool isSingleBit, size_t extend) const
    {
        if (!isSingleBit) {
            return (extend != 0);
        }
        if (Count % 16 == 0) {
            return true;
        }
        auto maxRegsCount = ((Count / 16) + 1) * 16;
        return (Count + extend) > maxRegsCount;
    }

    void TModbusRegisterRange::ReadRange(IModbusTraits& traits,
                                         TPort& port,
                                         uint8_t slaveId,
                                         int shift,
                                         Modbus::TRegisterCache& cache)
    {
        try {
            const auto& deviceConfig = *(Device()->DeviceConfig());
            if (GetCount() < deviceConfig.MinReadRegisters) {
                Count = deviceConfig.MinReadRegisters;
            }
            auto request = GetRequest(traits, slaveId, shift);
            port.SleepSinceLastInteraction(Device()->DeviceConfig()->RequestDelay);
            port.WriteBytes(request.data(), request.size());
            TResponse response(GetResponseSize(traits));
            auto readRes = ReadResponse(traits,
                                        port,
                                        request,
                                        response,
                                        Device()->DeviceConfig()->ResponseTimeout,
                                        Device()->DeviceConfig()->FrameTimeout);
            ResponseTime = readRes.ResponseTime;
            ParseReadResponse(traits.GetPDU(response), readRes.Count, *this, cache);
        } catch (const TMalformedResponseError&) {
            try {
                port.SkipNoise();
            } catch (const std::exception& e) {
                LOG(Warn) << "SkipNoise failed: " << e.what();
            }
            throw;
        }
    }

    std::chrono::microseconds TModbusRegisterRange::GetResponseTime() const
    {
        return ResponseTime;
    }

    ostream& operator<<(ostream& s, const TModbusRegisterRange& range)
    {
        s << range.GetCount() << " " << range.TypeName() << "(s) @ " << range.GetStart() << " of device "
          << range.Device()->ToString();
        return s;
    }

    const uint8_t EXCEPTION_BIT = 1 << 7;

    enum ModbusFunction : uint8_t
    {
        FN_READ_COILS = 0x1,
        FN_READ_DISCRETE = 0x2,
        FN_READ_HOLDING = 0x3,
        FN_READ_INPUT = 0x4,
        FN_WRITE_SINGLE_COIL = 0x5,
        FN_WRITE_SINGLE_REGISTER = 0x6,
        FN_WRITE_MULTIPLE_COILS = 0xF,
        FN_WRITE_MULTIPLE_REGISTERS = 0x10,
    };

    enum class OperationType : uint8_t
    {
        OP_READ = 0,
        OP_WRITE
    };

    bool IsException(const uint8_t* pdu)
    {
        return pdu[0] & EXCEPTION_BIT;
    }

    // returns modbus exception code if there is any, otherwise 0
    inline uint8_t GetExceptionCode(const uint8_t* pdu)
    {
        if (IsException(pdu)) {
            return pdu[1]; // then error code in the next byte
        }
        return 0;
    }

    // choose function code for modbus request
    uint8_t GetFunctionImpl(int registerType, OperationType op, const std::string& typeName, bool many)
    {
        switch (registerType) {
            case REG_HOLDING_SINGLE:
            case REG_HOLDING_MULTI:
            case REG_HOLDING:
                switch (op) {
                    case OperationType::OP_READ:
                        return FN_READ_HOLDING;
                    case OperationType::OP_WRITE:
                        return many ? FN_WRITE_MULTIPLE_REGISTERS : FN_WRITE_SINGLE_REGISTER;
                    default:
                        break;
                }
                break;
            case REG_INPUT:
                switch (op) {
                    case OperationType::OP_READ:
                        return FN_READ_INPUT;
                    default:
                        break;
                }
                break;
            case REG_COIL:
                switch (op) {
                    case OperationType::OP_READ:
                        return FN_READ_COILS;
                    case OperationType::OP_WRITE:
                        return many ? FN_WRITE_MULTIPLE_COILS : FN_WRITE_SINGLE_COIL;
                    default:
                        break;
                }
                break;
            case REG_DISCRETE:
                switch (op) {
                    case OperationType::OP_READ:
                        return FN_READ_DISCRETE;
                    default:
                        break;
                }
                break;
            default:
                break;
        }

        switch (op) {
            case OperationType::OP_READ:
                throw TSerialDeviceException("can't read from " + typeName);
            case OperationType::OP_WRITE:
                throw TSerialDeviceException("can't write to " + typeName);
            default:
                throw TSerialDeviceException("invalid operation type");
        }
    }

    inline uint8_t GetFunction(const TRegisterConfig& reg, OperationType op)
    {
        return GetFunctionImpl(reg.Type, op, reg.TypeName, IsPacking(reg));
    }

    inline uint8_t GetFunction(const TModbusRegisterRange& range, OperationType op)
    {
        return GetFunctionImpl(range.Type(), op, range.TypeName(), IsPacking(range));
    }

    // throws C++ exception on modbus error code
    void ThrowIfModbusException(uint8_t code)
    {
        if (code == 0) {
            return;
        }
        typedef std::pair<const char*, bool> TModbusException;
        // clang-format off
        const TModbusException errs[] =
            { {"illegal function",                        true },     // 0x01
              {"illegal data address",                    true },     // 0x02
              {"illegal data value",                      true },     // 0x03
              {"server device failure",                   false},     // 0x04
              {"long operation (acknowledge)",            false},     // 0x05
              {"server device is busy",                   false},     // 0x06
              {nullptr,                                   false},     // 0x07
              {"memory parity error",                     false},     // 0x08
              {nullptr,                                   false},     // 0x09
              {"gateway path is unavailable",             false},     // 0x0A
              {"gateway target device failed to respond", false}  };  // 0x0B
        // clang-format on
        --code;
        if (code < sizeof(errs) / sizeof(TModbusException)) {
            const auto& err = errs[code];
            if (err.first != nullptr) {
                if (err.second) {
                    throw TSerialDevicePermanentRegisterException(err.first);
                }
                throw TSerialDeviceTransientErrorException(err.first);
            }
        }
        throw TSerialDeviceTransientErrorException("invalid modbus error code (" + std::to_string(code + 1) + ")");
    }

    // returns count of modbus registers needed to represent TRegister
    uint16_t GetQuantity(const TRegister& reg)
    {
        int w = GetModbusDataWidthIn16BitWords(reg);

        if (IsSingleBitType(reg.Type)) {
            if (w != 1) {
                throw TSerialDeviceException("width other than 1 is not currently supported for reg type" +
                                             reg.TypeName);
            }
            return 1;
        } else {
            if (w > 4 && reg.GetDataOffset() == 0) {
                throw TSerialDeviceException("can't pack more than 4 " + reg.TypeName + "s into a single value");
            }
            return w;
        }
    }

    // returns count of modbus registers needed to represent TModbusRegisterRange
    uint16_t GetQuantity(const TModbusRegisterRange& range)
    {
        auto type = range.Type();

        if (!IsSingleBitType(type) && type != REG_HOLDING && type != REG_HOLDING_SINGLE && type != REG_HOLDING_MULTI &&
            type != REG_INPUT)
        {
            throw TSerialDeviceException("invalid register type");
        }

        return range.GetCount();
    }

    // returns number of bytes needed to hold request
    size_t InferWriteRequestPDUSize(const TRegister& reg)
    {
        return IsPacking(reg) ? 6 + GetModbusDataWidthIn16BitWords(reg) * 2 : 5;
    }

    // returns number of requests needed to write register
    size_t InferWriteRequestsCount(const TRegisterConfig& reg)
    {
        return IsPacking(reg) ? 1 : GetModbusDataWidthIn16BitWords(reg);
    }

    // returns number of bytes needed to hold response
    size_t InferReadResponsePDUSize(int type, size_t registerCount)
    {
        if (IsSingleBitType(type)) {
            return 2 + std::ceil(static_cast<float>(registerCount) / 8); // coil values are packed into bytes as bitset
        } else {
            return 2 + registerCount * 2; // count is for uint16_t, we need byte count
        }
    }

    inline size_t ReadResponsePDUSize(const uint8_t* pdu)
    {
        // Modbus stores data byte count in second byte of PDU,
        // so PDU size is data size + 2 (1b function code + 1b byte count itself)
        return IsException(pdu) ? EXCEPTION_RESPONSE_PDU_SIZE : pdu[1] + 2;
    }

    inline size_t WriteResponsePDUSize(const uint8_t* pdu)
    {
        return IsException(pdu) ? EXCEPTION_RESPONSE_PDU_SIZE : WRITE_RESPONSE_PDU_SIZE;
    }

    // fills pdu with read request data according to Modbus specification
    void ComposeReadRequestPDU(uint8_t* pdu, const TRegister& reg, int shift)
    {
        pdu[0] = GetFunction(reg, OperationType::OP_READ);
        auto addr = GetUint32RegisterAddress(reg.GetAddress());
        WriteAs2Bytes(pdu + 1, addr + shift);
        WriteAs2Bytes(pdu + 3, GetQuantity(reg));
    }

    void ComposeReadRequestPDU(uint8_t* pdu, const TModbusRegisterRange& range, int shift)
    {
        pdu[0] = GetFunction(range, OperationType::OP_READ);
        WriteAs2Bytes(pdu + 1, range.GetStart() + shift);
        WriteAs2Bytes(pdu + 3, GetQuantity(range));
    }

    // fills pdu with write request data according to Modbus specification
    void ComposeMultipleWriteRequestPDU(uint8_t* pdu,
                                        const TRegisterConfig& reg,
                                        const std::vector<TRegisterWord>& value,
                                        int shift,
                                        Modbus::TRegisterCache& tmpCache,
                                        const Modbus::TRegisterCache& cache)
    {
        pdu[0] = GetFunction(reg, OperationType::OP_WRITE);

        auto addr = GetUint32RegisterAddress(reg.GetWriteAddress());

        uint32_t widthInModbusWords = GetModbusDataWidthIn16BitWords(reg);

        auto baseAddress = addr + shift;

        TAddress address{0};

        address.Type = reg.Type;

        WriteAs2Bytes(pdu + 1, baseAddress);
        WriteAs2Bytes(pdu + 3, widthInModbusWords);

        pdu[5] = widthInModbusWords * 2;

        for (uint32_t i = 0; (i < widthInModbusWords) && (i < value.size()); ++i) {
            address.Address = baseAddress + i;

            auto data = value.at(i);
            tmpCache[address.AbsAddress] = data;
            WriteAs2Bytes(pdu + 6 + i * 2, data);
        }
    }

    // fills pdu with write request data according to Modbus specification
    void ComposeMultipleWriteRequestPDU(uint8_t* pdu,
                                        const TRegisterConfig& reg,
                                        uint64_t value,
                                        int shift,
                                        Modbus::TRegisterCache& tmpCache,
                                        const Modbus::TRegisterCache& cache)
    {
        pdu[0] = GetFunction(reg, OperationType::OP_WRITE);

        auto addr = GetUint32RegisterAddress(reg.GetWriteAddress());
        auto widthInModbusWords = GetModbusDataWidthIn16BitWords(reg);

        auto baseAddress = addr + shift;

        WriteAs2Bytes(pdu + 1, baseAddress);
        WriteAs2Bytes(pdu + 3, widthInModbusWords);

        pdu[5] = widthInModbusWords * 2;

        // Fill value from cache
        TAddress address{0};
        address.Type = reg.Type;
        address.Address = baseAddress;
        int step_k = 1;
        if (reg.WordOrder == EWordOrder::LittleEndian) {
            address.Address += widthInModbusWords - 1;
            step_k = -1;
        }
        uint64_t valueToWrite = 0;
        for (size_t i = 0; i < widthInModbusWords; ++i) {
            valueToWrite <<= 16;
            if (cache.count(address.AbsAddress)) {
                valueToWrite |= cache.at(address.AbsAddress);
            }
            address.Address += step_k;
        }

        // Clear place for data to be written
        valueToWrite &= ~(GetLSBMask(reg.GetDataWidth()) << reg.GetDataOffset());

        // Place data
        value <<= reg.GetDataOffset();
        valueToWrite |= value;

        for (size_t i = 0; i < widthInModbusWords; ++i) {
            address.Address = baseAddress + i;
            uint16_t wordValue = (reg.WordOrder == EWordOrder::BigEndian)
                                     ? (valueToWrite >> (widthInModbusWords - 1) * 16) & 0xFFFF
                                     : valueToWrite & 0xFFFF;

            tmpCache[address.AbsAddress] = wordValue;
            WriteAs2Bytes(pdu + 6 + i * 2, wordValue);
            if (reg.WordOrder == EWordOrder::BigEndian) {
                valueToWrite <<= 16;
            } else {
                valueToWrite >>= 16;
            }
        }
    }

    void ComposeSingleWriteRequestPDU(uint8_t* pdu,
                                      const TRegisterConfig& reg,
                                      TRegisterWord value,
                                      int shift,
                                      uint8_t wordIndex,
                                      Modbus::TRegisterCache& tmpCache,
                                      const Modbus::TRegisterCache& cache)
    {
        auto bitWidth = reg.GetDataWidth();
        if (reg.Type == REG_COIL) {
            value = value ? uint16_t(0xFF) << 8 : 0x00;
            bitWidth = 16;
        }

        TAddress address;

        address.Type = reg.Type;

        auto addr = GetUint32RegisterAddress(reg.GetWriteAddress());
        address.Address = addr + shift + wordIndex;

        uint16_t cachedValue = 0;
        if (cache.count(address.AbsAddress)) {
            cachedValue = cache.at(address.AbsAddress);
        }

        auto localBitOffset = std::max(static_cast<int32_t>(reg.GetDataOffset()) - wordIndex * 16, 0);

        auto bitCount = std::min(static_cast<uint32_t>(16 - localBitOffset), bitWidth);

        auto mask = GetLSBMask(bitCount) << localBitOffset;

        auto wordValue = (~mask & cachedValue) | (mask & (value << localBitOffset));

        tmpCache[address.AbsAddress] = wordValue & 0xffff;

        pdu[0] = GetFunction(reg, OperationType::OP_WRITE);

        WriteAs2Bytes(pdu + 1, address.Address);
        WriteAs2Bytes(pdu + 3, wordValue);
    }

    void ParseSingleBitReadResponse(const uint8_t* pdu, TModbusRegisterRange& range)
    {
        auto start = pdu + 2;
        uint8_t byte_count = pdu[1];
        auto end = start + byte_count;
        auto destination = range.GetBits();
        auto coil_count = range.GetCount();
        while (start != end) {
            std::bitset<8> coils(*start++);
            auto coils_in_byte = std::min(coil_count, 8);
            for (int i = 0; i < coils_in_byte; ++i) {
                destination[i] = coils[i];
            }

            coil_count -= coils_in_byte;
            destination += coils_in_byte;
        }

        for (auto reg: range.RegisterList()) {
            auto addr = GetUint32RegisterAddress(reg->GetAddress());
            reg->SetValue(TRegisterValue{range.GetBits()[addr - range.GetStart()]});
        }
        return;
    }

    uint64_t GetRegisterValueFromReadData(const uint16_t* rangeData, size_t rangeStartAddr, const TRegister& reg)
    {
        auto addr = GetUint32RegisterAddress(reg.GetAddress());
        int wordCount = GetModbusDataWidthIn16BitWords(reg);

        uint64_t r = 0;
        if (reg.WordOrder == EWordOrder::LittleEndian) {
            for (int i = wordCount - 1; i >= 0; --i) {
                r <<= 16;
                r |= rangeData[addr - rangeStartAddr + i];
            }
        } else {
            for (int i = 0; i < wordCount; ++i) {
                r <<= 16;
                r |= rangeData[addr - rangeStartAddr + i];
            }
        }

        r >>= reg.GetDataOffset();
        r &= GetLSBMask(reg.GetDataWidth());

        return r;
    }

    std::string GetStringRegisterValueFromReadData(const uint16_t* rangeData,
                                                   size_t rangeStartAddr,
                                                   const TRegister& reg)
    {
        auto addr = GetUint32RegisterAddress(reg.GetAddress());
        const auto registerDataSize = GetModbusDataWidthIn16BitWords(reg);
        std::string str;

        for (uint32_t i = 0; i < registerDataSize; ++i) {
            auto ch = static_cast<char>(rangeData[addr - rangeStartAddr + i]);
            if (ch != '\0') {
                str.push_back(ch);
            }
        }
        return str;
    }

    void FillCache(const uint8_t* pdu, TModbusRegisterRange& range, Modbus::TRegisterCache& cache)
    {
        TAddress address;
        address.Type = range.Type();
        auto baseAddress = range.GetStart();
        auto start = pdu + 2;
        uint8_t byte_count = pdu[1];
        auto data16BitWords = range.GetWords();
        for (int i = 0; i < byte_count / 2; ++i) {
            address.Address = baseAddress + i;
            cache[address.AbsAddress] = data16BitWords[i] = (*start << 8) | *(start + 1);
            start += 2;
        }
    }

    // parses modbus response and stores result
    void ParseReadResponse(const uint8_t* pdu,
                           size_t pduSize,
                           TModbusRegisterRange& range,
                           Modbus::TRegisterCache& cache)
    {
        ThrowIfModbusException(GetExceptionCode(pdu));

        uint8_t byte_count = pdu[1];
        if (pduSize - 2 < byte_count) {
            throw TMalformedResponseError("invalid read response byte count: " + std::to_string(byte_count) + ", got " +
                                          std::to_string(pduSize - 2));
        }

        if (IsSingleBitType(range.Type())) {
            ParseSingleBitReadResponse(pdu, range);
            return;
        }

        FillCache(pdu, range, cache);

        auto data16BitWords = range.GetWords();

        for (auto reg: range.RegisterList()) {
            if (reg->Format == RegisterFormat::String) {
                reg->SetValue(
                    TRegisterValue{GetStringRegisterValueFromReadData(data16BitWords, range.GetStart(), *reg)});
            } else {
                reg->SetValue(TRegisterValue{GetRegisterValueFromReadData(data16BitWords, range.GetStart(), *reg)});
            }
        }
    }

    // checks modbus response on write
    void ParseWriteResponse(const uint8_t* pdu, size_t pduSize)
    {
        ThrowIfModbusException(GetExceptionCode(pdu));
        auto pdu_size = WriteResponsePDUSize(pdu);

        if (pdu_size != pduSize) {
            throw TMalformedResponseError("invalid write response PDU size: " + std::to_string(pdu_size) +
                                          ", expected " + std::to_string(pduSize));
        }
    }

    PRegisterRange CreateRegisterRange(std::chrono::microseconds averageResponseTime)
    {
        return std::make_shared<TModbusRegisterRange>(averageResponseTime);
    }

    TReadFrameResult ReadResponse(IModbusTraits& traits,
                                  TPort& port,
                                  const TRequest& request,
                                  TResponse& response,
                                  std::chrono::milliseconds responseTimeout,
                                  std::chrono::milliseconds frameTimeout)
    {
        auto res = traits.ReadFrame(port, responseTimeout, frameTimeout, request, response);
        // PDU size must be at least 2 bytes
        if (res.Count < 2) {
            throw TMalformedResponseError("Wrong PDU size: " + to_string(res.Count));
        }
        auto requestFunctionCode = traits.GetPDU(request)[0];
        auto responseFunctionCode = traits.GetPDU(response)[0] & 127; // get actual function code even if exception

        if (requestFunctionCode != responseFunctionCode) {
            throw TSerialDeviceTransientErrorException("request and response function code mismatch");
        }
        return res;
    }

    void WriteRegister(IModbusTraits& traits,
                       TPort& port,
                       uint8_t slaveId,
                       uint32_t sn,
                       TRegister& reg,
                       const TRegisterValue& value,
                       Modbus::TRegisterCache& cache,
                       std::chrono::microseconds requestDelay,
                       std::chrono::milliseconds responseTimeout,
                       std::chrono::milliseconds frameTimeout,
                       int shift)
    {
        Modbus::TRegisterCache tmpCache;

        LOG(Debug) << "write " << GetModbusDataWidthIn16BitWords(reg) << " " << reg.TypeName << "(s) @ "
                   << reg.GetWriteAddress() << " of device "
                   << (reg.Device() ? reg.Device()->ToString() : std::to_string(slaveId));

        // 1 byte - function code, 2 bytes - register address, 2 bytes - value
        const uint16_t WRITE_RESPONSE_PDU_SIZE = 5;
        TResponse response(traits.GetPacketSize(WRITE_RESPONSE_PDU_SIZE));

        vector<TRequest> requests(InferWriteRequestsCount(reg));

        if (IsPacking(reg)) {
            auto& req = requests.front();
            req.resize(traits.GetPacketSize(InferWriteRequestPDUSize(reg)));

            assert(requests.size() == 1 && "only one request is expected when using multiple write");
            // Added workaround for data offset on write
            // Strings have their own writing procedure, which does not contain shifts.
            if (reg.Format == RegisterFormat::String) {
                auto str = value.Get<std::string>();
                std::vector<TRegisterWord> payloadBuf;
                std::for_each(str.begin(), str.end(), [&payloadBuf](char ch) { payloadBuf.push_back(ch); });
                ComposeMultipleWriteRequestPDU(traits.GetPDU(req), reg, payloadBuf, shift, tmpCache, cache);
            } else {
                ComposeMultipleWriteRequestPDU(traits.GetPDU(req), reg, value.Get<uint64_t>(), shift, tmpCache, cache);
            }
            traits.FinalizeRequest(req, slaveId, sn);
        } else {
            auto val = value.Get<uint64_t>();
            for (size_t i = 0; i < requests.size(); ++i) {
                auto& req = requests[i];
                req.resize(traits.GetPacketSize(InferWriteRequestPDUSize(reg)));

                ComposeSingleWriteRequestPDU(traits.GetPDU(req),
                                             reg,
                                             static_cast<uint16_t>(val & 0xffff),
                                             shift,
                                             requests.size() - i - 1,
                                             tmpCache,
                                             cache);

                val >>= 16;
                traits.FinalizeRequest(req, slaveId, sn);
            }
        }

        for (const auto& request: requests) {
            try {
                port.SleepSinceLastInteraction(requestDelay);
                port.WriteBytes(request.data(), request.size());
                auto pduSize = ReadResponse(traits, port, request, response, responseTimeout, frameTimeout).Count;
                ParseWriteResponse(traits.GetPDU(response), pduSize);
            } catch (const TMalformedResponseError&) {
                try {
                    port.SkipNoise();
                } catch (const std::exception& e) {
                    LOG(Warn) << "SkipNoise failed: " << e.what();
                }
                throw;
            }
        }

        for (const auto& item: tmpCache) {
            cache.insert_or_assign(item.first, item.second);
        }
    }

    void ProcessRangeException(TModbusRegisterRange& range, const char* msg)
    {
        for (auto& reg: range.RegisterList()) {
            reg->SetError(TRegister::TError::ReadError);
        }

        auto& logger = (range.Device()->GetConnectionState() == TDeviceConnectionState::DISCONNECTED) ? Debug : Warn;
        LOG(logger) << "failed to read " << range << ": " << msg;
        range.Device()->SetTransferResult(false);
    }

    void ReadRegisterRange(IModbusTraits& traits,
                           TPort& port,
                           uint8_t slaveId,
                           TModbusRegisterRange& range,
                           Modbus::TRegisterCache& cache,
                           int shift)
    {
        if (range.RegisterList().empty()) {
            return;
        }
        try {
            range.ReadRange(traits, port, slaveId, shift, cache);
            range.Device()->SetTransferResult(true);
        } catch (const TSerialDevicePermanentRegisterException& e) {
            if (range.HasHoles()) {
                range.Device()->SetSupportsHoles(false);
            } else {
                for (auto& reg: range.RegisterList()) {
                    reg->SetAvailable(TRegisterAvailability::UNAVAILABLE);
                }
            }
            ProcessRangeException(range, e.what());
        } catch (const TSerialDeviceException& e) {
            ProcessRangeException(range, e.what());
        }
    }

    void WarnFailedRegisterSetup(const PDeviceSetupItem& item, const char* msg)
    {
        LOG(Warn) << "failed to write: " << item->Register->ToString() << ": " << msg;
    }

    void WriteSetupRegisters(Modbus::IModbusTraits& traits,
                             TPort& port,
                             uint8_t slaveId,
                             uint32_t sn,
                             const std::vector<PDeviceSetupItem>& setupItems,
                             Modbus::TRegisterCache& cache,
                             std::chrono::microseconds requestDelay,
                             std::chrono::milliseconds responseTimeout,
                             std::chrono::milliseconds frameTimeout,
                             int shift)
    {
        for (const auto& item: setupItems) {
            try {
                WriteRegister(traits,
                              port,
                              slaveId,
                              sn,
                              *item->Register,
                              item->RawValue,
                              cache,
                              requestDelay,
                              responseTimeout,
                              frameTimeout,
                              shift);

                std::stringstream ss;
                ss << "Init: " << item->Name << ": setup register " << item->Register->ToString() << " <-- "
                   << item->HumanReadableValue;

                if (item->RawValue.GetType() == TRegisterValue::ValueType::String) {
                    ss << " ('" << item->RawValue << "')";
                } else {
                    ss << " (0x" << std::hex << item->RawValue << ")";
                    // TODO: More verbose exception
                }
                LOG(Info) << ss.str();
            } catch (const TSerialDevicePermanentRegisterException& e) {
                WarnFailedRegisterSetup(item, e.what());
            }
        }
        if (!setupItems.empty() && setupItems.front()->Register->Device()) {
            setupItems.front()->Register->Device()->SetTransferResult(true);
        }
    }

    // TModbusRTUTraits

    TModbusRTUTraits::TModbusRTUTraits(bool forceFrameTimeout): ForceFrameTimeout(forceFrameTimeout)
    {}

    TPort::TFrameCompletePred TModbusRTUTraits::ExpectNBytes(size_t n) const
    {
        return [=](uint8_t* buf, size_t size) {
            if (size < 2)
                return false;
            if (Modbus::IsException(buf + 1)) // GetPDU
                return size >= EXCEPTION_RESPONSE_PDU_SIZE + DATA_SIZE;
            return size >= n;
        };
    }

    size_t TModbusRTUTraits::GetPacketSize(size_t pduSize) const
    {
        return DATA_SIZE + pduSize;
    }

    void TModbusRTUTraits::FinalizeRequest(TRequest& request, uint8_t slaveId, uint32_t sn)
    {
        request[0] = slaveId;
        WriteAs2Bytes(&request[request.size() - 2], CRC16::CalculateCRC16(request.data(), request.size() - 2));
    }

    TReadFrameResult TModbusRTUTraits::ReadFrame(TPort& port,
                                                 const std::chrono::milliseconds& responseTimeout,
                                                 const std::chrono::milliseconds& frameTimeout,
                                                 const TRequest& req,
                                                 TResponse& res) const
    {
        auto rc = port.ReadFrame(res.data(),
                                 res.size(),
                                 responseTimeout + frameTimeout,
                                 frameTimeout,
                                 ExpectNBytes(res.size()));
        // RTU response should be at least 3 bytes: 1 byte slave_id, 2 bytes CRC
        if (rc.Count < DATA_SIZE) {
            throw Modbus::TMalformedResponseError("invalid data size");
        }

        uint16_t crc = (res[rc.Count - 2] << 8) + res[rc.Count - 1];
        if (crc != CRC16::CalculateCRC16(res.data(), rc.Count - 2)) {
            throw TInvalidCRCError();
        }

        if (ForceFrameTimeout) {
            std::array<uint8_t, 256> buf;
            try {
                port.ReadFrame(buf.data(), buf.size(), frameTimeout, frameTimeout);
            } catch (const TResponseTimeoutException& e) {
                // No extra data
            }
        }

        auto requestSlaveId = req[0];
        auto responseSlaveId = res[0];
        if (requestSlaveId != responseSlaveId) {
            throw TSerialDeviceTransientErrorException("request and response slave id mismatch");
        }
        rc.Count -= DATA_SIZE;
        return rc;
    }

    uint8_t* TModbusRTUTraits::GetPDU(std::vector<uint8_t>& frame) const
    {
        return &frame[1];
    }

    const uint8_t* TModbusRTUTraits::GetPDU(const std::vector<uint8_t>& frame) const
    {
        return &frame[1];
    }

    // TModbusTCPTraits

    TModbusTCPTraits::TModbusTCPTraits(std::shared_ptr<uint16_t> transactionId): TransactionId(transactionId)
    {}

    void TModbusTCPTraits::SetMBAP(TRequest& req, uint16_t transactionId, size_t pduSize, uint8_t slaveId) const
    {
        req[0] = ((transactionId >> 8) & 0xFF);
        req[1] = (transactionId & 0xFF);
        req[2] = 0; // MODBUS
        req[3] = 0;
        ++pduSize; // length includes additional byte of unit identifier
        req[4] = ((pduSize >> 8) & 0xFF);
        req[5] = (pduSize & 0xFF);
        req[6] = slaveId;
    }

    uint16_t TModbusTCPTraits::GetLengthFromMBAP(const TResponse& buf) const
    {
        uint16_t l = buf[4];
        l <<= 8;
        l += buf[5];
        return l;
    }

    size_t TModbusTCPTraits::GetPacketSize(size_t pduSize) const
    {
        return MBAP_SIZE + pduSize;
    }

    void TModbusTCPTraits::FinalizeRequest(TRequest& request, uint8_t slaveId, uint32_t sn)
    {
        ++(*TransactionId);
        SetMBAP(request, *TransactionId, request.size() - MBAP_SIZE, slaveId);
    }

    TReadFrameResult TModbusTCPTraits::ReadFrame(TPort& port,
                                                 const std::chrono::milliseconds& responseTimeout,
                                                 const std::chrono::milliseconds& frameTimeout,
                                                 const TRequest& req,
                                                 TResponse& res) const
    {
        auto startTime = chrono::steady_clock::now();
        while (chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - startTime) <
               responseTimeout + frameTimeout)
        {
            if (res.size() < MBAP_SIZE) {
                res.resize(MBAP_SIZE);
            }
            auto rc = port.ReadFrame(res.data(), MBAP_SIZE, responseTimeout + frameTimeout, frameTimeout);

            if (rc.Count < MBAP_SIZE) {
                throw TMalformedResponseError("Can't read full MBAP");
            }

            auto len = GetLengthFromMBAP(res);
            // MBAP length should be at least 1 byte for unit identifier
            if (len == 0) {
                throw TMalformedResponseError("Wrong MBAP length value: 0");
            }
            --len; // length includes one byte of unit identifier which is already in buffer

            if (len + MBAP_SIZE > res.size()) {
                res.resize(len + MBAP_SIZE);
            }

            rc = port.ReadFrame(res.data() + MBAP_SIZE, len, frameTimeout, frameTimeout);
            if (rc.Count != len) {
                throw TMalformedResponseError("Wrong PDU size: " + to_string(rc.Count) + ", expected " +
                                              to_string(len));
            }

            // check transaction id
            if (req[0] == res[0] && req[1] == res[1]) {
                // check unit identifier
                if (req[6] != res[6]) {
                    throw TSerialDeviceTransientErrorException("request and response unit identifier mismatch");
                }
                return rc;
            }

            LOG(Debug) << "Transaction id mismatch";
        }
        throw TResponseTimeoutException();
    }

    uint8_t* TModbusTCPTraits::GetPDU(std::vector<uint8_t>& frame) const
    {
        return &frame[MBAP_SIZE];
    }

    const uint8_t* TModbusTCPTraits::GetPDU(const std::vector<uint8_t>& frame) const
    {
        return &frame[MBAP_SIZE];
    }

    std::unique_ptr<Modbus::IModbusTraits> TModbusRTUTraitsFactory::GetModbusTraits(PPort port, bool forceFrameTimeout)
    {
        return std::make_unique<Modbus::TModbusRTUTraits>(forceFrameTimeout);
    }

    std::unique_ptr<Modbus::IModbusTraits> TModbusTCPTraitsFactory::GetModbusTraits(PPort port, bool forceFrameTimeout)
    {
        auto it = TransactionIds.find(port);
        if (it == TransactionIds.end()) {
            std::tie(it, std::ignore) = TransactionIds.insert({port, std::make_shared<uint16_t>(0)});
        }
        return std::make_unique<Modbus::TModbusTCPTraits>(it->second);
    }

    void EnableWbContinuousRead(PSerialDevice device,
                                IModbusTraits& traits,
                                TPort& port,
                                uint8_t slaveId,
                                TRegisterCache& cache)
    {
        auto reg = std::make_shared<TRegister>(device,
                                               TRegister::Create(Modbus::REG_HOLDING, ENABLE_CONTINUOUS_READ_REGISTER));
        try {
            Modbus::WriteRegister(traits,
                                  port,
                                  slaveId,
                                  0,
                                  *reg,
                                  TRegisterValue(1),
                                  cache,
                                  device->DeviceConfig()->RequestDelay,
                                  device->DeviceConfig()->ResponseTimeout,
                                  device->DeviceConfig()->FrameTimeout);
            LOG(Info) << "Continuous read enabled [slave_id is " << device->DeviceConfig()->SlaveId + "]";
            if (device->DeviceConfig()->MaxRegHole < MAX_HOLE_CONTINUOUS_16_BIT_REGISTERS) {
                device->DeviceConfig()->MaxRegHole = MAX_HOLE_CONTINUOUS_16_BIT_REGISTERS;
            }
            if (device->DeviceConfig()->MaxBitHole < MAX_HOLE_CONTINUOUS_1_BIT_REGISTERS) {
                device->DeviceConfig()->MaxBitHole = MAX_HOLE_CONTINUOUS_1_BIT_REGISTERS;
            }
        } catch (const TSerialDevicePermanentRegisterException& e) {
            // A firmware doesn't support continuous read
            LOG(Warn) << "Continuous read is not enabled [slave_id is " << device->DeviceConfig()->SlaveId + "]";
        }
    }

} // modbus protocol utilities
