#include "modbus_common.h"
#include "bin_utils.h"
#include "log.h"
#include "serial_device.h"

using namespace std;
using namespace BinUtils;

#define LOG(logger) logger.Log() << "[modbus] "

namespace Modbus // modbus protocol declarations
{
    const int MAX_READ_BITS = 2000;

    const int MAX_READ_REGISTERS = 125;

    const int MAX_HOLE_CONTINUOUS_16_BIT_REGISTERS = 10;
    const int MAX_HOLE_CONTINUOUS_1_BIT_REGISTERS = MAX_HOLE_CONTINUOUS_16_BIT_REGISTERS * 8;

    const uint16_t ENABLE_CONTINUOUS_READ_REGISTER = 114;

    union TAddress
    {
        int64_t AbsAddress;
        struct
        {
            int Type;
            int Address;
        };
    };

    size_t InferReadResponsePDUSize(int type, size_t registerCount);

    //! Parses modbus response and stores result
    void ParseReadResponse(const std::vector<uint8_t>& data,
                           Modbus::EFunction function,
                           TModbusRegisterRange& range,
                           TRegisterCache& cache);
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

    void RethrowSerialDeviceException(const Modbus::TModbusExceptionError& err)
    {
        if (err.GetExceptionCode() == 1 || err.GetExceptionCode() == 2 || err.GetExceptionCode() == 3) {
            throw TSerialDevicePermanentRegisterException(err.what());
        }
        throw TSerialDeviceTransientErrorException(err.what());
    }
} // general utilities

namespace Modbus // modbus protocol common utilities
{
    enum class OperationType : uint8_t
    {
        OP_READ = 0,
        OP_WRITE
    };

    EFunction GetFunctionImpl(int registerType, OperationType op, const std::string& typeName, bool many);

    TModbusRegisterRange::TModbusRegisterRange(std::chrono::microseconds averageResponseTime)
        : AverageResponseTime(averageResponseTime),
          ResponseTime(averageResponseTime)
    {}

    TModbusRegisterRange::~TModbusRegisterRange()
    {
        if (Bits)
            delete[] Bits;
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

    std::vector<uint16_t>& TModbusRegisterRange::GetWords()
    {
        if (IsSingleBitType(Type()))
            throw std::runtime_error("GetWords() for non-word register");
        if (Words.empty()) {
            Words.resize(Count);
        }
        return Words;
    }

    int TModbusRegisterRange::GetStart() const
    {
        return Start;
    }

    size_t TModbusRegisterRange::GetCount() const
    {
        return Count;
    }

    bool TModbusRegisterRange::HasHoles() const
    {
        return HasHolesFlg;
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
            auto function = GetFunctionImpl(Type(), OperationType::OP_READ, TypeName(), IsPacking(*this));
            auto pdu = Modbus::MakePDU(function, GetStart() + shift, Count, {});
            port.SleepSinceLastInteraction(Device()->DeviceConfig()->RequestDelay);
            auto res = traits.Transaction(port,
                                          slaveId,
                                          pdu,
                                          Modbus::CalcResponsePDUSize(function, Count),
                                          Device()->DeviceConfig()->ResponseTimeout,
                                          Device()->DeviceConfig()->FrameTimeout);
            ResponseTime = res.ResponseTime;
            ParseReadResponse(res.Pdu, function, *this, cache);
        } catch (const Modbus::TModbusExceptionError& err) {
            RethrowSerialDeviceException(err);
        } catch (const Modbus::TMalformedResponseError& err) {
            try {
                port.SkipNoise();
            } catch (const std::exception& e) {
                LOG(Warn) << "SkipNoise failed: " << e.what();
            }
            throw TSerialDeviceTransientErrorException(err.what());
        } catch (const Modbus::TErrorBase& err) {
            throw TSerialDeviceTransientErrorException(err.what());
        }
    }

    std::chrono::microseconds TModbusRegisterRange::GetResponseTime() const
    {
        return ResponseTime;
    }

    // returns count of modbus registers needed to represent TModbusRegisterRange
    uint16_t TModbusRegisterRange::GetQuantity() const
    {
        auto type = Type();

        if (!IsSingleBitType(type) && type != REG_HOLDING && type != REG_HOLDING_SINGLE && type != REG_HOLDING_MULTI &&
            type != REG_INPUT)
        {
            throw TSerialDeviceException("invalid register type");
        }

        return GetCount();
    }

    ostream& operator<<(ostream& s, const TModbusRegisterRange& range)
    {
        s << range.GetCount() << " " << range.TypeName() << "(s) @ " << range.GetStart() << " of device "
          << range.Device()->ToString();
        return s;
    }

    // choose function code for modbus request
    EFunction GetFunctionImpl(int registerType, OperationType op, const std::string& typeName, bool many)
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

    inline EFunction GetFunction(const TRegisterConfig& reg, OperationType op)
    {
        return GetFunctionImpl(reg.Type, op, reg.TypeName, IsPacking(reg));
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

    size_t ComposeStringWriteRequestData(std::vector<uint8_t>& data,
                                         const TRegisterConfig& reg,
                                         const std::string& str,
                                         uint16_t baseAddress,
                                         Modbus::TRegisterCache& tmpCache)
    {
        uint32_t regCount = std::min(GetModbusDataWidthIn16BitWords(reg), static_cast<uint32_t>(str.size()));
        data.resize(regCount * 2);
        TAddress address{0};
        address.Type = reg.Type;
        for (uint32_t i = 0; (i < regCount); ++i) {
            address.Address = baseAddress + i;
            uint16_t regData = str[i];
            tmpCache[address.AbsAddress] = regData;
            WriteAs2Bytes(data.data() + i * 2, regData);
        }
        return regCount;
    }

    size_t ComposeRawMultipleWriteRequestData(std::vector<uint8_t>& data,
                                              const TRegisterConfig& reg,
                                              uint64_t value,
                                              uint16_t baseAddress,
                                              Modbus::TRegisterCache& tmpCache,
                                              const Modbus::TRegisterCache& cache)
    {

        auto widthInModbusWords = GetModbusDataWidthIn16BitWords(reg);
        data.resize(widthInModbusWords * 2);

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
            WriteAs2Bytes(data.data() + i * 2, wordValue);
            if (reg.WordOrder == EWordOrder::BigEndian) {
                valueToWrite <<= 16;
            } else {
                valueToWrite >>= 16;
            }
        }
        return widthInModbusWords;
    }

    void ComposeRawSingleWriteRequestData(std::vector<uint8_t>& data,
                                          const TRegisterConfig& reg,
                                          uint16_t value,
                                          uint16_t baseAddress,
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
        address.Address = baseAddress + wordIndex;

        uint16_t cachedValue = 0;
        if (cache.count(address.AbsAddress)) {
            cachedValue = cache.at(address.AbsAddress);
        }

        auto localBitOffset = std::max(static_cast<int32_t>(reg.GetDataOffset()) - wordIndex * 16, 0);

        auto bitCount = std::min(static_cast<uint32_t>(16 - localBitOffset), bitWidth);

        auto mask = GetLSBMask(bitCount) << localBitOffset;

        auto wordValue = (~mask & cachedValue) | (mask & (value << localBitOffset));

        tmpCache[address.AbsAddress] = wordValue & 0xffff;

        data.resize(2);
        WriteAs2Bytes(data.data(), wordValue);
    }

    void ParseSingleBitReadResponse(const std::vector<uint8_t>& data, TModbusRegisterRange& range)
    {
        auto start = data.begin();
        auto destination = range.GetBits();
        auto coil_count = range.GetCount();
        while (start != data.end()) {
            std::bitset<8> coils(*start++);
            auto coils_in_byte = std::min(coil_count, size_t(8));
            for (size_t i = 0; i < coils_in_byte; ++i) {
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

    uint64_t GetRegisterValueFromReadData(const std::vector<uint16_t>& rangeData,
                                          size_t rangeStartAddr,
                                          const TRegisterConfig& reg)
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

    std::string GetStringRegisterValueFromReadData(const std::vector<uint16_t>& rangeData,
                                                   size_t rangeStartAddr,
                                                   const TRegisterConfig& reg)
    {
        const auto addr = GetUint32RegisterAddress(reg.GetAddress());
        if (rangeStartAddr > addr) {
            return std::string();
        }

        const auto startPosInRange = addr - rangeStartAddr;
        if (startPosInRange >= rangeData.size()) {
            return std::string();
        }
        auto startIt = rangeData.begin() + startPosInRange;

        const auto registerDataSize = GetModbusDataWidthIn16BitWords(reg);
        const auto endPosInRange = std::min(startPosInRange + registerDataSize, rangeData.size());
        auto endIt = rangeData.begin() + endPosInRange;

        std::string str;
        for (; startIt != endIt; ++startIt) {
            auto ch = static_cast<char>(*startIt);
            if (ch == '\0' || ch == 0xFF) {
                break;
            }
            str.push_back(ch);
        }
        return str;
    }

    void FillRegisters(std::vector<uint16_t>& registers, const std::vector<uint8_t>& data)
    {
        auto size = std::min(data.size() / 2, registers.size());
        auto start = data.data();
        for (size_t i = 0; i < size; ++i) {
            registers[i] = (*start << 8) | *(start + 1);
            start += 2;
        }
    }

    void FillCache(const std::vector<uint8_t>& data, TModbusRegisterRange& range, Modbus::TRegisterCache& cache)
    {
        TAddress address;
        address.Type = range.Type();
        auto baseAddress = range.GetStart();
        auto& data16BitWords = range.GetWords();
        FillRegisters(data16BitWords, data);
        for (size_t i = 0; i < data.size() / 2; ++i) {
            address.Address = baseAddress + i;
            cache[address.AbsAddress] = data16BitWords[i];
        }
    }

    // parses modbus response and stores result
    void ParseReadResponse(const std::vector<uint8_t>& pdu,
                           Modbus::EFunction function,
                           TModbusRegisterRange& range,
                           Modbus::TRegisterCache& cache)
    {
        auto data = Modbus::ExtractResponseData(function, pdu);
        if (IsSingleBitType(range.Type())) {
            ParseSingleBitReadResponse(data, range);
            return;
        }

        FillCache(data, range, cache);

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

    PRegisterRange CreateRegisterRange(std::chrono::microseconds averageResponseTime)
    {
        return std::make_shared<TModbusRegisterRange>(averageResponseTime);
    }

    void WriteTransaction(IModbusTraits& traits,
                          TPort& port,
                          uint8_t slaveId,
                          Modbus::EFunction fn,
                          size_t responsePduSize,
                          const std::vector<uint8_t>& pdu,
                          std::chrono::microseconds requestDelay,
                          std::chrono::milliseconds responseTimeout,
                          std::chrono::milliseconds frameTimeout)
    {
        try {
            port.SleepSinceLastInteraction(requestDelay);
            auto res = traits.Transaction(port, slaveId, pdu, responsePduSize, responseTimeout, frameTimeout);
            Modbus::ExtractResponseData(fn, res.Pdu);
        } catch (const Modbus::TModbusExceptionError& err) {
            RethrowSerialDeviceException(err);
        } catch (const Modbus::TMalformedResponseError& err) {
            try {
                port.SkipNoise();
            } catch (const std::exception& e) {
                LOG(Warn) << "SkipNoise failed: " << e.what();
            }
            throw TSerialDeviceTransientErrorException(err.what());
        } catch (const Modbus::TErrorBase& err) {
            throw TSerialDeviceTransientErrorException(err.what());
        }
    }

    void WriteRegister(IModbusTraits& traits,
                       TPort& port,
                       uint8_t slaveId,
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

        std::vector<uint8_t> data;
        auto addr = GetUint32RegisterAddress(reg.GetWriteAddress()) + shift;
        auto fn = GetFunction(reg, OperationType::OP_WRITE);
        if (IsPacking(reg)) {
            size_t regCount =
                reg.Format == RegisterFormat::String
                    ? ComposeStringWriteRequestData(data, reg, value.Get<std::string>(), addr, tmpCache)
                    : ComposeRawMultipleWriteRequestData(data, reg, value.Get<uint64_t>(), addr, tmpCache, cache);
            WriteTransaction(traits,
                             port,
                             slaveId,
                             fn,
                             Modbus::CalcResponsePDUSize(fn, regCount),
                             Modbus::MakePDU(fn, addr, regCount, data),
                             requestDelay,
                             responseTimeout,
                             frameTimeout);
        } else {
            auto requestsCount = InferWriteRequestsCount(reg);
            auto val = value.Get<uint64_t>();
            auto responsePduSize = Modbus::CalcResponsePDUSize(fn, 1);
            for (size_t i = 0; i < requestsCount; ++i) {
                auto wordIndex = requestsCount - i - 1;
                ComposeRawSingleWriteRequestData(data,
                                                 reg,
                                                 static_cast<uint16_t>(val & 0xffff),
                                                 addr,
                                                 wordIndex,
                                                 tmpCache,
                                                 cache);
                WriteTransaction(traits,
                                 port,
                                 slaveId,
                                 fn,
                                 responsePduSize,
                                 Modbus::MakePDU(fn, addr + wordIndex, 1, data),
                                 requestDelay,
                                 responseTimeout,
                                 frameTimeout);
                val >>= 16;
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

    TRegisterValue ReadRegister(IModbusTraits& traits,
                                TPort& port,
                                uint8_t slaveId,
                                const TRegisterConfig& registerConfig,
                                std::chrono::microseconds requestDelay,
                                std::chrono::milliseconds responseTimeout,
                                std::chrono::milliseconds frameTimeout)
    {
        size_t modbusRegisterCount = GetModbusDataWidthIn16BitWords(registerConfig);
        auto addr = GetUint32RegisterAddress(registerConfig.GetAddress());
        auto function = GetFunctionImpl(registerConfig.Type, OperationType::OP_READ, registerConfig.TypeName, false);
        auto requestPdu = Modbus::MakePDU(function, addr, modbusRegisterCount, {});

        port.SleepSinceLastInteraction(requestDelay);
        auto res = traits.Transaction(port,
                                      slaveId,
                                      requestPdu,
                                      Modbus::CalcResponsePDUSize(function, modbusRegisterCount),
                                      responseTimeout,
                                      frameTimeout);

        auto responseData = Modbus::ExtractResponseData(function, res.Pdu);

        if (IsSingleBitType(registerConfig.Type)) {
            return TRegisterValue{responseData[0]};
        }

        std::vector<uint16_t> data16BitWords(modbusRegisterCount);
        FillRegisters(data16BitWords, responseData);
        if (registerConfig.Format == RegisterFormat::String) {
            return TRegisterValue{GetStringRegisterValueFromReadData(data16BitWords, addr, registerConfig)};
        }
        return TRegisterValue{GetRegisterValueFromReadData(data16BitWords, addr, registerConfig)};
    }

} // modbus protocol utilities
