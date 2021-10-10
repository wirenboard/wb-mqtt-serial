#include "modbus_common.h"
#include "serial_device.h"
#include "crc16.h"
#include "log.h"
#include "bin_utils.h"

#include <cmath>
#include <array>
#include <cassert>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <math.h>

using namespace std;
using namespace BinUtils;

#define LOG(logger) logger.Log() << "[modbus] "

namespace Modbus // modbus protocol declarations
{
    const int MAX_READ_BITS = 2000;
    const int MAX_WRITE_BITS = 1968;

    const int MAX_READ_REGISTERS = 125;
    const int MAX_WRITE_REGISTERS = 123;
    const int MAX_RW_WRITE_REGISTERS = 121;

    const size_t EXCEPTION_RESPONSE_PDU_SIZE = 2;
    const size_t WRITE_RESPONSE_PDU_SIZE = 5;

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

    class TModbusRegisterRange;
    void ComposeReadRequestPDU(uint8_t* pdu, TModbusRegisterRange& range, int shift);
    size_t InferReadResponsePDUSize(TModbusRegisterRange& range);

    class TModbusRegisterRange: public TRegisterRange
    {
    public:
        TModbusRegisterRange(const std::list<PRegister>& regs, bool hasHoles);
        ~TModbusRegisterRange();
        EStatus GetStatus() const override;
        void SetStatus(EStatus status);
        int GetStart() const
        {
            return Start;
        }
        int GetCount() const
        {
            return Count;
        }
        uint8_t* GetBits();
        uint16_t* GetWords();

        bool HasHoles() const
        {
            return HasHolesFlg;
        }

        // Read each register separately
        bool ShouldReadOneByOne() const
        {
            return ReadOneByOne;
        }
        void SetReadOneByOne(bool readOneByOne)
        {
            ReadOneByOne = readOneByOne;
        }

        const TRequest& GetRequest(IModbusTraits& traits, uint8_t slaveId, int shift)
        {
            if (Request.empty()) {
                // 1 byte - function code, 2 bytes - starting register address, 2 bytes - quantity of registers
                const uint16_t REQUEST_PDU_SIZE = 5;

                Request.resize(traits.GetPacketSize(REQUEST_PDU_SIZE));
                Modbus::ComposeReadRequestPDU(traits.GetPDU(Request), *this, shift);
                traits.FinalizeRequest(Request, slaveId);
            }
            return Request;
        }

        size_t GetResponseSize(IModbusTraits& traits)
        {
            if (ResponseSize == 0) {
                ResponseSize = traits.GetPacketSize(InferReadResponsePDUSize(*this));
            }
            return ResponseSize;
        }

    private:
        bool ReadOneByOne = false;
        bool HasHolesFlg = false;
        int Start;
        int Count;
        uint8_t* Bits = 0;
        uint16_t* Words = 0;
        EStatus Status = ST_UNKNOWN_ERROR;
        TRequest Request;
        size_t ResponseSize = 0;
    };

    using PModbusRegisterRange = std::shared_ptr<TModbusRegisterRange>;
}

namespace // general utilities
{
    // write 16-bit value to byte array in big-endian order
    inline void WriteAs2Bytes(uint8_t* dst, uint16_t val)
    {
        dst[0] = static_cast<uint8_t>(val >> 8);
        dst[1] = static_cast<uint8_t>(val);
    }

    // returns true if multi write needs to be done
    inline bool IsPacking(const TRegister& reg)
    {
        return (reg.Type == Modbus::REG_HOLDING_MULTI) ||
               ((reg.Type == Modbus::REG_HOLDING) && (reg.Get16BitWidth() > 1));
    }

    inline bool IsPacking(Modbus::TModbusRegisterRange& range)
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

    class TInvalidCRCError: public TMalformedResponseError
    {
    public:
        TInvalidCRCError(): TMalformedResponseError("invalid crc")
        {}
    };

    TModbusRegisterRange::TModbusRegisterRange(const std::list<PRegister>& regs, bool hasHoles)
        : TRegisterRange(regs),
          HasHolesFlg(hasHoles)
    {
        if (regs.empty()) // shouldn't happen
            throw std::runtime_error("cannot construct empty register range");

        if (IsSingleBitType(Type())) {
            for (auto reg: RegisterList()) {
                if (reg->Get16BitWidth() != 1)
                    throw TSerialDeviceException("width other than 1 is not currently supported for reg type" +
                                                 reg->TypeName);
            }
        }

        auto it = regs.begin();
        Start = GetUint32RegisterAddress((*it)->GetAddress());
        int end = Start + (*it)->Get16BitWidth();
        while (++it != regs.end()) {
            if ((*it)->Type != Type())
                throw std::runtime_error("registers of different type in the same range");
            auto addr = GetUint32RegisterAddress((*it)->GetAddress());
            int new_end = addr + (*it)->Get16BitWidth();
            if (new_end > end)
                end = new_end;
        }
        Count = end - Start;
        if (Count > (IsSingleBitType(Type()) ? MAX_READ_BITS : MAX_READ_REGISTERS))
            throw std::runtime_error("Modbus register range too large");
    }

    void TModbusRegisterRange::SetStatus(EStatus status)
    {
        Status = status;
    }

    EStatus TModbusRegisterRange::GetStatus() const
    {
        return Status;
    }

    TModbusRegisterRange::~TModbusRegisterRange()
    {
        if (Bits)
            delete[] Bits;
        if (Words)
            delete[] Words;
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

    inline bool IsException(const uint8_t* pdu)
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

    inline uint8_t GetFunction(const TRegister& reg, OperationType op)
    {
        return GetFunctionImpl(reg.Type, op, reg.TypeName, IsPacking(reg));
    }

    inline uint8_t GetFunction(TModbusRegisterRange& range, OperationType op)
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
    uint16_t GetQuantity(TRegister& reg)
    {
        int w = reg.Get16BitWidth();

        if (IsSingleBitType(reg.Type)) {
            if (w != 1) {
                throw TSerialDeviceException("width other than 1 is not currently supported for reg type" +
                                             reg.TypeName);
            }
            return 1;
        } else {
            if (w > 4 && reg.BitOffset == 0) {
                throw TSerialDeviceException("can't pack more than 4 " + reg.TypeName + "s into a single value");
            }
            return w;
        }
    }

    // returns count of modbus registers needed to represent TModbusRegisterRange
    uint16_t GetQuantity(TModbusRegisterRange& range)
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
        return IsPacking(reg) ? 6 + reg.Get16BitWidth() * 2 : 5;
    }

    // returns number of requests needed to write register
    size_t InferWriteRequestsCount(const TRegister& reg)
    {
        return IsPacking(reg) ? 1 : reg.Get16BitWidth();
    }

    // returns number of bytes needed to hold response
    size_t InferReadResponsePDUSize(TModbusRegisterRange& range)
    {
        auto count = range.GetCount();

        if (IsSingleBitType(range.Type())) {
            return 2 + std::ceil(static_cast<float>(count) / 8); // coil values are packed into bytes as bitset
        } else {
            return 2 + count * 2; // count is for uint16_t, we need byte count
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
    void ComposeReadRequestPDU(uint8_t* pdu, TRegister& reg, int shift)
    {
        pdu[0] = GetFunction(reg, OperationType::OP_READ);
        auto addr = GetUint32RegisterAddress(reg.GetAddress());
        WriteAs2Bytes(pdu + 1, addr + shift);
        WriteAs2Bytes(pdu + 3, GetQuantity(reg));
    }

    void ComposeReadRequestPDU(uint8_t* pdu, TModbusRegisterRange& range, int shift)
    {
        pdu[0] = GetFunction(range, OperationType::OP_READ);
        WriteAs2Bytes(pdu + 1, range.GetStart() + shift);
        WriteAs2Bytes(pdu + 3, GetQuantity(range));
    }

    // fills pdu with write request data according to Modbus specification
    void ComposeMultipleWriteRequestPDU(uint8_t* pdu, const TRegister& reg, uint64_t value, int shift)
    {
        auto& tmpCache = reg.Device()->ModbusTmpCache;
        const auto& cache = reg.Device()->ModbusCache;

        pdu[0] = GetFunction(reg, OperationType::OP_WRITE);

        auto addr = GetUint32RegisterAddress(reg.GetAddress());
        auto baseAddress = addr + shift;
        const auto bitWidth = reg.GetBitWidth();

        auto bitsToAllocate = bitWidth;

        TAddress address;

        address.Type = reg.Type;

        WriteAs2Bytes(pdu + 1, baseAddress);
        WriteAs2Bytes(pdu + 3, reg.Get16BitWidth());

        pdu[5] = reg.Get16BitWidth() * 2;

        uint8_t bitPos = 0, bitPosEnd = bitWidth;

        for (int i = 0; i < reg.Get16BitWidth(); ++i) {
            address.Address = baseAddress + i;

            uint16_t cachedValue;
            if (cache.count(address.AbsAddress)) {
                cachedValue = cache.at(address.AbsAddress);
            } else {
                cachedValue = value & 0xffff;
            }

            auto localBitOffset = std::max(reg.BitOffset - bitPos, 0);

            auto bitCount = std::min(uint8_t(16 - localBitOffset), bitsToAllocate);

            auto rBitPos = bitPosEnd - bitPos - bitCount;

            auto mask = GetLSBMask(bitCount);

            auto valuePart = mask & (value >> rBitPos);

            auto wordValue = (~mask & cachedValue) | (valuePart << localBitOffset);

            tmpCache[address.AbsAddress] = wordValue & 0xffff;

            WriteAs2Bytes(pdu + 6 + i * 2, wordValue & 0xffff);
            bitsToAllocate -= bitCount;
            bitPos += bitCount;
        }
    }

    void ComposeSingleWriteRequestPDU(uint8_t* pdu, const TRegister& reg, uint16_t value, int shift, uint8_t wordIndex)
    {
        auto& tmpCache = reg.Device()->ModbusTmpCache;
        const auto& cache = reg.Device()->ModbusCache;

        if (reg.Type == REG_COIL) {
            value = value ? uint16_t(0xFF) << 8 : 0x00;
        }

        auto bitWidth = reg.GetBitWidth();

        TAddress address;

        address.Type = reg.Type;
        auto addr = GetUint32RegisterAddress(reg.GetAddress());
        address.Address = addr + shift + wordIndex;

        uint16_t cachedValue;
        if (cache.count(address.AbsAddress)) {
            cachedValue = cache.at(address.AbsAddress);
        } else {
            cachedValue = value & 0xffff;
        }

        auto localBitOffset = std::max(reg.BitOffset - wordIndex * 16, 0);

        auto bitCount = std::min(uint8_t(16 - localBitOffset), bitWidth);

        auto mask = GetLSBMask(bitCount) << localBitOffset;

        auto wordValue = (~mask & cachedValue) | (mask & (value << localBitOffset));

        tmpCache[address.AbsAddress] = wordValue & 0xffff;

        pdu[0] = GetFunction(reg, OperationType::OP_WRITE);

        WriteAs2Bytes(pdu + 1, address.Address);
        WriteAs2Bytes(pdu + 3, wordValue);
    }

    // parses modbus response and stores result
    void ParseReadResponse(const uint8_t* pdu, size_t pduSize, TModbusRegisterRange& range)
    {
        TAddress address;

        address.Type = range.Type();

        auto& cache = range.Device()->ModbusCache;
        auto baseAddress = range.GetStart();

        ThrowIfModbusException(GetExceptionCode(pdu));

        uint8_t byte_count = pdu[1];

        if (pduSize - 2 < byte_count) {
            throw TMalformedResponseError("invalid read response byte count: " + std::to_string(byte_count) + ", got " +
                                          std::to_string(pduSize - 2));
        }

        auto start = pdu + 2;
        auto end = start + byte_count;
        if (IsSingleBitType(range.Type())) {
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
                reg->SetValue(range.GetBits()[addr - range.GetStart()]);
            }
            return;
        }

        auto destination = range.GetWords();
        for (int i = 0; i < byte_count / 2; ++i) {
            address.Address = baseAddress + i;

            cache[address.AbsAddress] = destination[i] = (*start << 8) | *(start + 1);

            start += 2;
        }

        for (auto reg: range.RegisterList()) {
            int w = reg->Get16BitWidth();
            auto bitWidth = reg->GetBitWidth();

            uint64_t r = 0;

            auto addr = GetUint32RegisterAddress(reg->GetAddress());
            int wordIndex = (addr - range.GetStart());
            auto reverseWordIndex = w - 1;

            uint8_t bitsWritten = 0;

            while (w--) {
                uint16_t data = destination[addr - range.GetStart() + w];

                auto localBitOffset = std::max(reg->BitOffset - wordIndex * 16, 0);

                auto bitCount = std::min(uint8_t(16 - localBitOffset), bitWidth);

                auto mask = GetLSBMask(bitCount);

                r |= (mask & (data >> localBitOffset)) << bitsWritten;

                --reverseWordIndex;
                ++wordIndex;
                bitWidth -= bitCount;
                bitsWritten += bitCount;
            }
            if ((reg->UnsupportedValue) && (*reg->UnsupportedValue == r)) {
                reg->SetError(ST_DEVICE_ERROR);
                reg->SetAvailable(false);
            } else {
                reg->SetValue(r);
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

    std::list<PRegisterRange> SplitRegisterList(const std::list<PRegister>& reg_list,
                                                const TDeviceConfig& deviceConfig,
                                                bool enableHoles)
    {
        std::list<PRegisterRange> r;
        if (reg_list.empty())
            return r;

        std::list<PRegister> l;
        int prev_start = -1, prev_type = -1, prev_end = -1;
        std::chrono::milliseconds prev_interval;
        int max_hole =
            enableHoles ? (IsSingleBitType(reg_list.front()->Type) ? deviceConfig.MaxBitHole : deviceConfig.MaxRegHole)
                        : 0;
        int max_regs;

        if (IsSingleBitType(reg_list.front()->Type)) {
            max_regs = MAX_READ_BITS;
        } else {
            if ((deviceConfig.MaxReadRegisters > 0) && (deviceConfig.MaxReadRegisters <= MAX_READ_REGISTERS)) {
                max_regs = deviceConfig.MaxReadRegisters;
            } else {
                max_regs = MAX_READ_REGISTERS;
            }
        }

        bool hasHoles = false;
        for (auto reg: reg_list) {
            int addr = GetUint32RegisterAddress(reg->GetAddress());
            int new_end = addr + reg->Get16BitWidth();
            if (!(prev_end >= 0 && reg->Type == prev_type && addr >= prev_end && addr <= prev_end + max_hole &&
                  reg->PollInterval == prev_interval && new_end - prev_start <= max_regs))
            {
                if (!l.empty()) {
                    auto range = std::make_shared<TModbusRegisterRange>(l, hasHoles);
                    hasHoles = false;
                    LOG(Debug) << "Adding range: " << *range;
                    r.push_back(range);
                    l.clear();
                }
                prev_start = addr;
                prev_type = reg->Type;
                prev_interval = reg->PollInterval;
            }
            if (!l.empty()) {
                hasHoles |= (addr != prev_end);
            }
            l.push_back(reg);
            prev_end = new_end;
        }
        if (!l.empty()) {
            auto range = std::make_shared<TModbusRegisterRange>(l, hasHoles);
            LOG(Debug) << "Adding range: " << *range;
            r.push_back(range);
        }
        return r;
    }

    IModbusTraits::~IModbusTraits()
    {}

    size_t ProcessRequest(IModbusTraits& traits,
                          TPort& port,
                          const TRequest& request,
                          TResponse& response,
                          const TDeviceConfig& config)
    {
        port.SleepSinceLastInteraction(config.RequestDelay);
        port.WriteBytes(request.data(), request.size());

        auto res = traits.ReadFrame(port, config.ResponseTimeout, config.FrameTimeout, request, response);
        // PDU size must be at least 2 bytes
        if (res < 2) {
            throw TMalformedResponseError("Wrong PDU size: " + to_string(res));
        }
        auto requestFunctionCode = traits.GetPDU(request)[0];
        auto responseFunctionCode = traits.GetPDU(response)[0] & 127; // get actual function code even if exception

        if (requestFunctionCode != responseFunctionCode) {
            throw TSerialDeviceTransientErrorException("request and response function code mismatch");
        }
        return res;
    }

    void WriteRegister(IModbusTraits& traits, TPort& port, uint8_t slaveId, TRegister& reg, uint64_t value, int shift)
    {
        reg.Device()->DismissTmpCache();

        std::unique_ptr<TRegister, std::function<void(TRegister*)>> tmpCacheGuard(&reg, [](TRegister* reg) {
            reg->Device()->DismissTmpCache();
        });

        LOG(Debug) << "write " << reg.Get16BitWidth() << " " << reg.TypeName << "(s) @ " << reg.GetAddress()
                   << " of device " << reg.Device()->ToString();

        // 1 byte - function code, 2 bytes - register address, 2 bytes - value
        const uint16_t WRITE_RESPONSE_PDU_SIZE = 5;
        TResponse response(traits.GetPacketSize(WRITE_RESPONSE_PDU_SIZE));

        vector<TRequest> requests(InferWriteRequestsCount(reg));

        for (size_t i = 0; i < requests.size(); ++i) {
            auto& req = requests[i];
            req.resize(traits.GetPacketSize(InferWriteRequestPDUSize(reg)));

            if (IsPacking(reg)) {
                assert(requests.size() == 1 && "only one request is expected when using multiple write");
                ComposeMultipleWriteRequestPDU(traits.GetPDU(req), reg, value, shift);
            } else {
                ComposeSingleWriteRequestPDU(traits.GetPDU(req),
                                             reg,
                                             static_cast<uint16_t>(value & 0xffff),
                                             shift,
                                             requests.size() - i - 1);
                value >>= 16;
            }

            traits.FinalizeRequest(req, slaveId);
        }

        for (const auto& request: requests) {
            try {
                auto pduSize = ProcessRequest(traits, port, request, response, *reg.Device()->DeviceConfig());
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

        reg.Device()->ApplyTmpCache();
    }

    void ReadRange(IModbusTraits& traits, TModbusRegisterRange& range, TPort& port, uint8_t slaveId, int shift)
    {
        range.SetStatus(ST_UNKNOWN_ERROR);

        TResponse response(range.GetResponseSize(traits));
        try {
            const auto& request = range.GetRequest(traits, slaveId, shift);
            auto pduSize = ProcessRequest(traits, port, request, response, *range.Device()->DeviceConfig());
            ParseReadResponse(traits.GetPDU(response), pduSize, range);
            range.SetStatus(ST_OK);
        } catch (const TMalformedResponseError&) {
            try {
                port.SkipNoise();
            } catch (const std::exception& e) {
                LOG(Warn) << "SkipNoise failed: " << e.what();
            }
            range.SetStatus(ST_UNKNOWN_ERROR);
            throw;
        } catch (const TSerialDevicePermanentRegisterException&) {
            range.SetStatus(ST_DEVICE_ERROR);
            throw;
        } catch (const TSerialDeviceTransientErrorException&) {
            range.SetStatus(ST_UNKNOWN_ERROR);
            throw;
        }
    }

    void ProcessRangeException(TModbusRegisterRange& range, const char* msg, EStatus error)
    {
        range.SetError(error);
        auto& logger = range.Device()->GetIsDisconnected() ? Debug : Warn;
        LOG(logger) << "failed to read " << range << ": " << msg;
    }

    struct TTruncatedRegisterList
    {
        bool IsValid = false;
        std::list<PRegister> Regs;
    };

    // Remove unsupported registers on borders
    TTruncatedRegisterList RemoveUnsupportedFromBorders(const std::list<PRegister>& l)
    {
        TTruncatedRegisterList res;
        auto s = std::find_if(l.begin(), l.end(), [](auto& r) { return r->IsAvailable(); });
        auto e = std::find_if(l.rbegin(), std::make_reverse_iterator(s), [](auto& r) { return r->IsAvailable(); });
        if ((s != l.begin()) || (e != l.rbegin())) {
            std::copy(s, e.base(), std::back_inserter(res.Regs));
            res.IsValid = true;
        }
        return res;
    }

    std::list<PRegisterRange> SplitRangeByHoles(const std::list<PRegister>& regs, bool onlyAvailable)
    {
        std::list<PRegisterRange> newRanges;
        std::list<PRegister> l;
        PRegister lastReg;
        for (auto& reg: regs) {
            if (!l.empty()) {
                auto addr = GetUint32RegisterAddress(reg->GetAddress());
                auto lastAddr = GetUint32RegisterAddress(lastReg->GetAddress());
                if (lastAddr + 1 != addr) {
                    newRanges.push_back(std::make_shared<Modbus::TModbusRegisterRange>(l, false));
                    l.clear();
                }
            }
            if (!onlyAvailable || reg->IsAvailable()) {
                lastReg = reg;
                l.push_back(reg);
            }
        }
        if (!l.empty()) {
            newRanges.push_back(std::make_shared<Modbus::TModbusRegisterRange>(l, false));
        }
        return newRanges;
    }

    std::list<PRegisterRange> ReadWholeRange(Modbus::IModbusTraits& traits,
                                             Modbus::PModbusRegisterRange& range,
                                             TPort& port,
                                             uint8_t slaveId,
                                             int shift)
    {
        std::list<PRegisterRange> newRanges;
        try {
            ReadRange(traits, *range, port, slaveId, shift);
            auto res = RemoveUnsupportedFromBorders(range->RegisterList());
            if (res.IsValid) {
                if (!res.Regs.empty()) {
                    auto newRange = std::make_shared<Modbus::TModbusRegisterRange>(res.Regs, range->HasHoles());
                    newRange->SetStatus(range->GetStatus());
                    newRanges.push_back(newRange);
                }
            } else {
                newRanges.push_back(range);
            }
        } catch (const TSerialDeviceTransientErrorException& e) {
            ProcessRangeException(*range, e.what(), ST_UNKNOWN_ERROR);
            newRanges.push_back(range);
        } catch (const TSerialDevicePermanentRegisterException& e) {
            ProcessRangeException(*range, e.what(), ST_DEVICE_ERROR);
            if (range->HasHoles()) {
                LOG(Debug) << "Disabling holes feature for " << *range;
                return SplitRangeByHoles(range->RegisterList(), false);
            }
            range->SetReadOneByOne(true);
            newRanges.push_back(range);
        }
        return newRanges;
    }

    std::list<PRegisterRange> ReadOneByOne(Modbus::IModbusTraits& traits,
                                           Modbus::PModbusRegisterRange& range,
                                           TPort& port,
                                           uint8_t slaveId,
                                           int shift)
    {
        range->SetStatus(ST_UNKNOWN_ERROR);
        std::list<Modbus::PModbusRegisterRange> subRanges;
        for (auto& reg: range->RegisterList()) {
            subRanges.push_back(std::make_shared<Modbus::TModbusRegisterRange>(std::list<PRegister>{reg}, false));
        }
        for (auto& r: subRanges) {
            try {
                ReadRange(traits, *r, port, slaveId, shift);
            } catch (const TSerialDeviceTransientErrorException& e) {
                ProcessRangeException(*range, e.what(), ST_UNKNOWN_ERROR);
                return std::list<PRegisterRange>{range};
            } catch (const TSerialDevicePermanentRegisterException& e) {
                r->RegisterList().front()->SetAvailable(false);
                r->RegisterList().front()->SetError(ST_DEVICE_ERROR);
                LOG(Warn) << "Register " << r->RegisterList().front()->ToString() << " is not supported";
            }
        }
        range->SetStatus(ST_OK);
        return SplitRangeByHoles(range->RegisterList(), true);
        ;
    }

    std::list<PRegisterRange> ReadRegisterRange(Modbus::IModbusTraits& traits,
                                                TPort& port,
                                                uint8_t slaveId,
                                                PRegisterRange range,
                                                int shift)
    {
        auto modbus_range = std::dynamic_pointer_cast<Modbus::TModbusRegisterRange>(range);
        if (!modbus_range) {
            throw std::runtime_error("modbus range expected");
        }

        LOG(Debug) << "read " << *modbus_range;

        if (modbus_range->ShouldReadOneByOne()) {
            return ReadOneByOne(traits, modbus_range, port, slaveId, shift);
        }
        return ReadWholeRange(traits, modbus_range, port, slaveId, shift);
    }

    void WarnFailedRegisterSetup(const PDeviceSetupItem& item, const char* msg)
    {
        LOG(Warn) << "failed to write: " << item->Register->ToString() << ": " << msg;
    }

    bool WriteSetupRegisters(Modbus::IModbusTraits& traits,
                             TPort& port,
                             uint8_t slaveId,
                             const std::vector<PDeviceSetupItem>& setupItems,
                             int shift)
    {
        for (const auto& item: setupItems) {
            try {
                WriteRegister(traits, port, slaveId, *item->Register, item->RawValue, shift);
                LOG(Info) << "Init: " << item->Name << ": setup register " << item->Register->ToString() << " <-- "
                          << item->HumanReadableValue << " (0x" << std::hex << item->RawValue << ")";
            } catch (const TSerialDevicePermanentRegisterException& e) {
                WarnFailedRegisterSetup(item, e.what());
            } catch (const TSerialDeviceException& e) {
                WarnFailedRegisterSetup(item, e.what());
                return false;
            }
        }
        return true;
    }

    // TModbusRTUTraits

    TPort::TFrameCompletePred TModbusRTUTraits::ExpectNBytes(int n) const
    {
        return [=](uint8_t* buf, int size) {
            if (size < 2)
                return false;
            if (Modbus::IsException(buf + 1)) // GetPDU
                return size >= static_cast<int>(EXCEPTION_RESPONSE_PDU_SIZE + DATA_SIZE);
            return size >= n;
        };
    }

    size_t TModbusRTUTraits::GetPacketSize(size_t pduSize) const
    {
        return DATA_SIZE + pduSize;
    }

    void TModbusRTUTraits::FinalizeRequest(TRequest& request, uint8_t slaveId)
    {
        request[0] = slaveId;
        WriteAs2Bytes(&request[request.size() - 2], CRC16::CalculateCRC16(request.data(), request.size() - 2));
    }

    size_t TModbusRTUTraits::ReadFrame(TPort& port,
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
        if (rc < DATA_SIZE) {
            throw Modbus::TMalformedResponseError("invalid data size");
        }

        uint16_t crc = (res[rc - 2] << 8) + res[rc - 1];
        if (crc != CRC16::CalculateCRC16(res.data(), rc - 2)) {
            throw TInvalidCRCError();
        }

        auto requestSlaveId = req[0];
        auto responseSlaveId = res[0];
        if (requestSlaveId != responseSlaveId) {
            throw TSerialDeviceTransientErrorException("request and response slave id mismatch");
        }
        return rc - DATA_SIZE;
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

    void TModbusTCPTraits::FinalizeRequest(TRequest& request, uint8_t slaveId)
    {
        ++(*TransactionId);
        SetMBAP(request, *TransactionId, request.size() - MBAP_SIZE, slaveId);
    }

    size_t TModbusTCPTraits::ReadFrame(TPort& port,
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

            if (rc < MBAP_SIZE) {
                throw TMalformedResponseError("Can't read full MBAP");
            }

            auto len = GetLengthFromMBAP(res);
            // MBAP length should be at least 1 byte for unit indentifier
            if (len == 0) {
                throw TMalformedResponseError("Wrong MBAP length value: 0");
            }
            --len; // length includes one byte of unit identifier which is already in buffer

            if (len + MBAP_SIZE > res.size()) {
                res.resize(len + MBAP_SIZE);
            }

            rc = port.ReadFrame(res.data() + MBAP_SIZE, len, frameTimeout, frameTimeout);
            if (rc != len) {
                throw TMalformedResponseError("Wrong PDU size: " + to_string(rc) + ", expected " + to_string(len));
            }

            // check transaction id
            if (req[0] == res[0] && req[1] == res[1]) {
                // check unit indentifier
                if (req[6] != res[6]) {
                    throw TSerialDeviceTransientErrorException("request and response unit indentifier mismatch");
                }
                return rc;
            }

            LOG(Debug) << "Transaction id mismatch";
        }
        throw TSerialDeviceTransientErrorException("request timed out");
    }

    uint8_t* TModbusTCPTraits::GetPDU(std::vector<uint8_t>& frame) const
    {
        return &frame[MBAP_SIZE];
    }

    const uint8_t* TModbusTCPTraits::GetPDU(const std::vector<uint8_t>& frame) const
    {
        return &frame[MBAP_SIZE];
    }

    std::unique_ptr<Modbus::IModbusTraits> TModbusRTUTraitsFactory::GetModbusTraits(PPort /*port*/)
    {
        return std::make_unique<Modbus::TModbusRTUTraits>();
    }

    std::unique_ptr<Modbus::IModbusTraits> TModbusTCPTraitsFactory::GetModbusTraits(PPort port)
    {
        auto it = TransactionIds.find(port);
        if (it == TransactionIds.end()) {
            std::tie(it, std::ignore) = TransactionIds.insert({port, std::make_shared<uint16_t>(0)});
        }
        return std::make_unique<Modbus::TModbusTCPTraits>(it->second);
    }
} // modbus protocol utilities
