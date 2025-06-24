#include "modbus_common.h"
#include "bin_utils.h"
#include "log.h"
#include "serial_device.h"
#include "wb_registers.h"

using namespace std;
using namespace BinUtils;

#define LOG(logger) logger.Log() << "[modbus] "

namespace Modbus // modbus protocol declarations
{
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

    // write 16-bit value to byte array in specified order
    inline void WriteAs2Bytes(uint8_t* dst, uint16_t val, EByteOrder byteOrder)
    {
        if (byteOrder == EByteOrder::LittleEndian) {
            dst[0] = static_cast<uint8_t>(val);
            dst[1] = static_cast<uint8_t>(val >> 8);
        } else {
            dst[0] = static_cast<uint8_t>(val >> 8);
            dst[1] = static_cast<uint8_t>(val);
        }
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

    inline bool IsHoldingType(int type)
    {
        return (type == Modbus::REG_HOLDING) || (type == Modbus::REG_HOLDING_SINGLE) ||
               (type == Modbus::REG_HOLDING_MULTI);
    }

    void RethrowSerialDeviceException(const Modbus::TModbusExceptionError& err)
    {
        if (err.GetExceptionCode() == Modbus::ILLEGAL_FUNCTION ||
            err.GetExceptionCode() == Modbus::ILLEGAL_DATA_ADDRESS ||
            err.GetExceptionCode() == Modbus::ILLEGAL_DATA_VALUE)
        {
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
        bool isSingleBit = IsSingleBitType(reg->GetConfig()->Type);
        auto addr = GetUint32RegisterAddress(reg->GetConfig()->GetAddress());
        const auto widthInWords = GetModbusDataWidthIn16BitWords(*reg->GetConfig());

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

        auto newPduSize = InferReadResponsePDUSize(reg->GetConfig()->Type, Count + extend);
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
                       << "RequestDelay=" << deviceConfig.RequestDelay.count() << " us, "
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

    uint32_t TModbusRegisterRange::GetStart() const
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
        return RegisterList().front()->GetConfig()->TypeName;
    }

    int TModbusRegisterRange::Type() const
    {
        return RegisterList().front()->GetConfig()->Type;
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

    // Composes array of data words filled with string chars according to string register format.
    // Word order corresponds to the order of string chars.
    // Byte order for "String8" format also corresponds to the order of string chars.
    // For "String" format every char placed to least significant byte of every word.
    void ComposeStringWriteRequestWords(std::vector<uint16_t>& words,
                                        const TRegisterConfig& reg,
                                        const std::string& str)
    {
        auto size = reg.Format == RegisterFormat::String8 ? str.size() / 2 + str.size() % 2 : str.size();
        auto width = std::min(GetModbusDataWidthIn16BitWords(reg), static_cast<uint32_t>(size));
        words.resize(width);
        for (uint32_t i = 0; i < width; ++i) {
            words[i] = reg.Format == RegisterFormat::String8 ? str[i * 2] << 8 | str[i * 2 + 1] : str[i];
        }
    }

    // Composes array of data words filled with numeric register value.
    // Uses holding register data cache to fill data that is not affected by the new register value.
    // Word order corresponds to defaul Modbus order (big endian).
    // Byte order corresponds to value byte order.
    void ComposeNumberWriteRequestWords(std::vector<uint16_t>& words,
                                        const TRegisterConfig& reg,
                                        uint64_t value,
                                        const Modbus::TRegisterCache& cache,
                                        Modbus::TRegisterCache& tmpCache)
    {
        uint32_t address = 0;
        uint64_t valueToWrite = 0;
        auto width = GetModbusDataWidthIn16BitWords(reg);
        auto step = 1;
        auto updateCache = false;

        if (reg.AccessType != TRegisterConfig::EAccessType::WRITE_ONLY) {
            address = GetUint32RegisterAddress(reg.GetAddress());
            if (reg.WordOrder == EWordOrder::LittleEndian) {
                address += width - 1;
                step = -1;
            }
            auto cacheAddress = address;
            for (uint32_t i = 0; i < width; ++i) {
                valueToWrite <<= 16;
                if (cache.count(cacheAddress)) {
                    valueToWrite |= cache.at(cacheAddress);
                }
                cacheAddress += step;
            }
            // Clear place for data to be written
            valueToWrite &= ~(GetLSBMask(reg.GetDataWidth()) << reg.GetDataOffset());
            updateCache = true;
        }

        // Place data
        value <<= reg.GetDataOffset();
        valueToWrite |= value;

        words.resize(width);
        for (uint32_t i = 0; i < width; ++i) {
            words[i] = (valueToWrite >> (width - 1) * 16) & 0xFFFF;
            valueToWrite <<= 16;
            if (updateCache) {
                tmpCache[address] = words[i];
                address += step;
            }
        }
    }

    // Composes writing data buffer containing multiple words register data.
    // Word order and byte order corresponds to register settings.
    size_t ComposeRawMultipleWriteRequestData(std::vector<uint8_t>& data,
                                              const TRegisterConfig& reg,
                                              const TRegisterValue& value,
                                              const Modbus::TRegisterCache& cache,
                                              Modbus::TRegisterCache& tmpCache)
    {
        std::vector<uint16_t> words;
        if (reg.IsString()) {
            ComposeStringWriteRequestWords(words, reg, value.Get<std::string>());
        } else {
            ComposeNumberWriteRequestWords(words, reg, value.Get<uint64_t>(), cache, tmpCache);
        }
        if (reg.WordOrder == EWordOrder::LittleEndian) {
            std::reverse(words.begin(), words.end());
        }
        auto offset = data.size();
        auto width = words.size();
        data.resize(offset + width * 2);
        for (size_t i = 0; i < width; ++i) {
            WriteAs2Bytes(data.data() + offset + i * 2, words[i], reg.ByteOrder);
        }
        return width;
    }

    // Composes writing data buffer containtig single word filled with "partial" register data.
    // Uses holding register data cache to fill data that is not affected by the new register value.
    // Byte order corresponds to register settings.
    void ComposeRawSingleWriteRequestData(std::vector<uint8_t>& data,
                                          const TRegisterConfig& reg,
                                          uint16_t value,
                                          uint8_t wordIndex,
                                          const Modbus::TRegisterCache& cache,
                                          Modbus::TRegisterCache& tmpCache)
    {
        uint16_t cachedValue = 0;
        auto address = 0;
        auto bitWidth = reg.GetDataWidth();
        auto updateCache = false;

        // use cache only for "partial" registers
        if (bitWidth < 16) {
            address = GetUint32RegisterAddress(reg.GetAddress()) + wordIndex;
            if (cache.count(address)) {
                cachedValue = cache.at(address);
            }
            updateCache = true;
        }

        auto bitOffset = std::max(static_cast<int32_t>(reg.GetDataOffset()) - wordIndex * 16, 0);
        auto bitCount = std::min(static_cast<uint32_t>(16 - bitOffset), bitWidth);
        auto mask = GetLSBMask(bitCount) << bitOffset;
        auto valueToWrite = (~mask & cachedValue) | (mask & (value << bitOffset));

        data.resize(2);
        WriteAs2Bytes(data.data(), valueToWrite, reg.ByteOrder);

        if (updateCache) {
            tmpCache[address] = valueToWrite;
        }
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
            auto addr = GetUint32RegisterAddress(reg->GetConfig()->GetAddress());
            reg->SetValue(TRegisterValue{range.GetBits()[addr - range.GetStart()]});
        }
        return;
    }

    // Extracts numeric register data from data words array, ordered according
    // to the register word order and byte order settings.
    uint64_t GetNumberRegisterValue(const std::vector<uint16_t>& words, const TRegisterConfig& reg)
    {
        uint64_t value = 0;
        for (size_t i = 0; i < words.size(); ++i) {
            value <<= 16;
            value |= words[i];
        }
        value >>= reg.GetDataOffset();
        value &= GetLSBMask(reg.GetDataWidth());
        return value;
    }

    // Extracts string register data from data words array, ordered according
    // to the register word order and byte order settings.
    std::string GetStringRegisterValue(const std::vector<uint16_t>& words, const TRegisterConfig& reg)
    {
        std::string str;
        size_t offset = reg.Format == RegisterFormat::String8 ? 1 : 0;
        size_t shift = offset;
        auto it = words.begin();
        while (it != words.end()) {
            auto ch = static_cast<char>(*it >> shift * 8);
            if (ch == '\0' || ch == '\xFF') {
                break;
            }
            str.push_back(ch);
            if (shift > 0) {
                --shift;
                continue;
            }
            shift = offset;
            ++it;
        }
        return str;
    }

    // Orders read data buffer according to register word order and byte order settings.
    // Fills register data cache for holding registers.
    // Returns TRegister value.
    TRegisterValue GetRegisterValue(const std::vector<uint8_t>& data,
                                    const TRegisterConfig& reg,
                                    Modbus::TRegisterCache& cache,
                                    uint32_t index = 0)
    {
        auto address = GetUint32RegisterAddress(reg.GetAddress());
        auto width = GetModbusDataWidthIn16BitWords(reg);
        auto start = data.data() + index * 2;
        std::vector<uint16_t> words(width);
        for (uint32_t i = 0; i < width; i++) {
            if (reg.ByteOrder == EByteOrder::LittleEndian) {
                words[i] = *(start + 1) << 8 | *start;
            } else {
                words[i] = *start << 8 | *(start + 1);
            }
            if (IsHoldingType(reg.Type)) {
                cache[address] = words[i];
                ++address;
            }
            start += 2;
        }
        if (reg.WordOrder == EWordOrder::LittleEndian) {
            std::reverse(words.begin(), words.end());
        }
        return reg.IsString() ? TRegisterValue{GetStringRegisterValue(words, reg)}
                              : TRegisterValue{GetNumberRegisterValue(words, reg)};
    }

    // Parses modbus response and stores result.
    void ParseReadResponse(const std::vector<uint8_t>& pdu,
                           Modbus::EFunction function,
                           TModbusRegisterRange& range,
                           Modbus::TRegisterCache& cache)
    {
        auto data = Modbus::ExtractResponseData(function, pdu);
        range.Device()->SetTransferResult(true);
        if (IsSingleBitType(range.Type())) {
            ParseSingleBitReadResponse(data, range);
            return;
        }
        for (auto reg: range.RegisterList()) {
            auto config = reg->GetConfig();
            auto index = GetUint32RegisterAddress(config->GetAddress()) - range.GetStart();
            reg->SetValue(GetRegisterValue(data, *config, cache, index));
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
                       const TRegisterConfig& reg,
                       const TRegisterValue& value,
                       Modbus::TRegisterCache& cache,
                       std::chrono::microseconds requestDelay,
                       std::chrono::milliseconds responseTimeout,
                       std::chrono::milliseconds frameTimeout,
                       int shift)
    {
        Modbus::TRegisterCache tmpCache;

        LOG(Debug) << port.GetDescription() << " modbus:" << std::to_string(slaveId) << " write "
                   << GetModbusDataWidthIn16BitWords(reg) << " " << reg.TypeName << "(s) @ " << reg.GetWriteAddress();

        std::vector<uint8_t> data;
        auto addr = GetUint32RegisterAddress(reg.GetWriteAddress()) + shift;
        auto fn = GetFunction(reg, OperationType::OP_WRITE);
        if (IsPacking(reg)) {
            size_t regCount = ComposeRawMultipleWriteRequestData(data, reg, value, cache, tmpCache);
            WriteTransaction(traits,
                             port,
                             slaveId,
                             fn,
                             Modbus::CalcResponsePDUSize(fn, regCount),
                             Modbus::MakePDU(fn, addr, regCount, data),
                             requestDelay,
                             responseTimeout,
                             frameTimeout);
        } else if (reg.Type == REG_COIL) {
            const std::vector<uint8_t> on{0xFF, 0x00};
            const std::vector<uint8_t> off{0x00, 0x00};
            WriteTransaction(traits,
                             port,
                             slaveId,
                             fn,
                             Modbus::CalcResponsePDUSize(fn, 1),
                             Modbus::MakePDU(fn, addr, 1, value.Get<uint64_t>() ? on : off),
                             requestDelay,
                             responseTimeout,
                             frameTimeout);
        } else {
            // Note: word order feature currently is not supported by HOLDING_SINGLE registers.
            auto requestsCount = InferWriteRequestsCount(reg);
            auto val = value.Get<uint64_t>();
            auto responsePduSize = Modbus::CalcResponsePDUSize(fn, 1);
            for (size_t i = 0; i < requestsCount; ++i) {
                auto wordIndex = requestsCount - i - 1;
                ComposeRawSingleWriteRequestData(data, reg, static_cast<uint16_t>(val), wordIndex, cache, tmpCache);
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
        } catch (const TSerialDevicePermanentRegisterException& e) {
            if (range.HasHoles()) {
                range.Device()->SetSupportsHoles(false);
            } else {
                for (auto& reg: range.RegisterList()) {
                    reg->SetAvailable(TRegisterAvailability::UNAVAILABLE);
                    LOG(Warn) << reg->ToString() << " is now marked as unavailable: " << e.what();
                }
            }
            ProcessRangeException(range, e.what());
        } catch (const TSerialDeviceException& e) {
            ProcessRangeException(range, e.what());
        }
    }

    bool FillSetupRegistersCache(Modbus::IModbusTraits& traits,
                                 TPort& port,
                                 uint8_t slaveId,
                                 const TDeviceSetupItems& setupItems,
                                 Modbus::TRegisterCache& cache,
                                 int shift)
    {
        auto it = setupItems.begin();
        while (it != setupItems.end()) {
            TModbusRegisterRange range(std::chrono::microseconds::max());
            while (it != setupItems.end()) {
                auto item = *it;
                if (!item->RegisterConfig->IsPartial()) {
                    ++it;
                    continue;
                }
                auto address = GetUint32RegisterAddress(item->RegisterConfig->GetAddress());
                auto cached = true;
                for (uint32_t i = 0; i < item->RegisterConfig->Get16BitWidth(); ++i) {
                    if (cache.count(address) == 0) {
                        cached = false;
                        break;
                    }
                    ++address;
                }
                if (cached) {
                    ++it;
                    continue;
                }
                auto reg = std::make_shared<TRegister>(item->Device, item->RegisterConfig);
                reg->SetAvailable(TRegisterAvailability::AVAILABLE);
                if (!range.Add(reg, std::chrono::milliseconds::max())) {
                    break;
                }
                ++it;
            }
            ReadRegisterRange(traits, port, slaveId, range, cache, shift);
            for (auto reg: range.RegisterList()) {
                if (reg->GetErrorState().count()) {
                    return false;
                }
            }
        }
        return true;
    }

    TDeviceSetupItems::iterator WriteMultipleSetupRegisters(Modbus::IModbusTraits& traits,
                                                            TPort& port,
                                                            uint8_t slaveId,
                                                            TDeviceSetupItems::iterator startIt,
                                                            TDeviceSetupItems::iterator endIt,
                                                            size_t maxRegs,
                                                            Modbus::TRegisterCache& cache,
                                                            std::chrono::microseconds requestDelay,
                                                            std::chrono::milliseconds responseTimeout,
                                                            std::chrono::milliseconds frameTimeout,
                                                            int shift)
    {
        Modbus::TRegisterCache tmpCache;
        std::vector<uint8_t> data;
        PDeviceSetupItem last = nullptr;
        auto start = GetUint32RegisterAddress((*startIt)->RegisterConfig->GetWriteAddress());
        auto count = 0;
        while (startIt != endIt) {
            auto item = *startIt;
            auto address = GetUint32RegisterAddress(item->RegisterConfig->GetWriteAddress());
            if (last) {
                auto lastAddress = GetUint32RegisterAddress(last->RegisterConfig->GetWriteAddress());
                auto lastWidth = GetModbusDataWidthIn16BitWords(*last->RegisterConfig);
                if (item->RegisterConfig->Type != last->RegisterConfig->Type || address != lastAddress + lastWidth) {
                    break;
                }
            }

            auto width = GetModbusDataWidthIn16BitWords(*item->RegisterConfig);
            if (count && count + width > maxRegs) {
                break;
            }

            ComposeRawMultipleWriteRequestData(data, *item->RegisterConfig, item->RawValue, cache, tmpCache);
            LOG(Info) << item->ToString();

            count += width;
            last = item;
            ++startIt;
        }
        try {
            auto function = count > 1 ? FN_WRITE_MULTIPLE_REGISTERS : FN_WRITE_SINGLE_REGISTER;
            WriteTransaction(traits,
                             port,
                             slaveId,
                             function,
                             Modbus::CalcResponsePDUSize(function, count),
                             Modbus::MakePDU(function, start + shift, count, data),
                             requestDelay,
                             responseTimeout,
                             frameTimeout);
            for (const auto& item: tmpCache) {
                cache.insert_or_assign(item.first, item.second);
            }
        } catch (const TSerialDevicePermanentRegisterException& e) {
            LOG(Warn) << "Failed to write " << count << " setup items starting from register address " << start << ": "
                      << e.what();
        }
        return startIt;
    }

    void WriteSetupRegisters(Modbus::IModbusTraits& traits,
                             TPort& port,
                             uint8_t slaveId,
                             const TDeviceSetupItems& setupItems,
                             Modbus::TRegisterCache& cache,
                             std::chrono::microseconds requestDelay,
                             std::chrono::milliseconds responseTimeout,
                             std::chrono::milliseconds frameTimeout,
                             int shift)
    {
        if (!FillSetupRegistersCache(traits, port, slaveId, setupItems, cache, shift)) {
            throw TSerialDeviceException("unable to write setup registers because unable to read data needed to set "
                                         "values of \"partial\" setup registers");
        }
        auto it = setupItems.begin();
        while (it != setupItems.end()) {
            auto item = *it;
            auto config = item->Device->DeviceConfig();
            size_t maxRegs = MAX_WRITE_REGISTERS;
            if (config->MaxWriteRegisters > 0 && config->MaxWriteRegisters < maxRegs) {
                maxRegs = config->MaxWriteRegisters;
            }
            auto type = item->RegisterConfig->Type;
            if (maxRegs > 1 && type == REG_HOLDING) {
                it = WriteMultipleSetupRegisters(traits,
                                                 port,
                                                 slaveId,
                                                 it,
                                                 setupItems.end(),
                                                 maxRegs,
                                                 cache,
                                                 requestDelay,
                                                 responseTimeout,
                                                 frameTimeout,
                                                 shift);
            } else {
                try {
                    WriteRegister(traits,
                                  port,
                                  slaveId,
                                  *item->RegisterConfig,
                                  item->RawValue,
                                  cache,
                                  requestDelay,
                                  responseTimeout,
                                  frameTimeout,
                                  shift);
                    LOG(Info) << item->ToString();
                } catch (const TSerialDevicePermanentRegisterException& e) {
                    LOG(Warn) << "Failed to write setup item \"" << item->Name << "\": " << e.what();
                }
                ++it;
            }
        }
        if (!setupItems.empty()) {
            auto item = *setupItems.begin();
            if (item->Device) {
                item->Device->SetTransferResult(true);
            }
        }
    }

    void EnableWbContinuousRead(PSerialDevice device,
                                IModbusTraits& traits,
                                TPort& port,
                                uint8_t slaveId,
                                TRegisterCache& cache)
    {
        auto config = WbRegisters::GetRegisterConfig("continuous_read");
        try {
            Modbus::WriteRegister(traits,
                                  port,
                                  slaveId,
                                  *config,
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
                                const TRegisterConfig& reg,
                                std::chrono::microseconds requestDelay,
                                std::chrono::milliseconds responseTimeout,
                                std::chrono::milliseconds frameTimeout)
    {
        size_t modbusRegisterCount = GetModbusDataWidthIn16BitWords(reg);
        auto addr = GetUint32RegisterAddress(reg.GetAddress());
        auto function = GetFunctionImpl(reg.Type, OperationType::OP_READ, reg.TypeName, false);
        auto requestPdu = Modbus::MakePDU(function, addr, modbusRegisterCount, {});

        port.SleepSinceLastInteraction(requestDelay);
        auto res = traits.Transaction(port,
                                      slaveId,
                                      requestPdu,
                                      Modbus::CalcResponsePDUSize(function, modbusRegisterCount),
                                      responseTimeout,
                                      frameTimeout);

        auto data = Modbus::ExtractResponseData(function, res.Pdu);
        if (IsSingleBitType(reg.Type)) {
            return TRegisterValue{data[0]};
        }

        Modbus::TRegisterCache cache;
        return GetRegisterValue(data, reg, cache);
    }

} // modbus protocol utilities
