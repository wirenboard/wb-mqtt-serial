#include "modbus_common.h"
#include "serial_device.h"
#include "crc16.h"

#include <cmath>
#include <array>
#include <unistd.h>


namespace   // general utilities
{
    // write 16-bit value to byte array in big-endian order
    inline void WriteAs2Bytes(uint8_t* dst, uint16_t val)
    {
        dst[0] = static_cast<uint8_t>(val >> 8);
        dst[1] = static_cast<uint8_t>(val);
    }

    // returns true if more than one modbus register is needed to represent TRegister
    inline bool IsPacking(PRegister reg)
    {
        return (reg->Type == Modbus::REG_HOLDING) && (reg->Width() > 1);
    }

    inline bool IsSingleBitType(int type)
    {
        return (type == Modbus::REG_COIL) || (type == Modbus::REG_DISCRETE);
    }
}   // general utilities


namespace Modbus    // modbus protocol common utilities
{
    const int MAX_READ_BITS = 2000;
    const int MAX_WRITE_BITS = 1968;

    const int MAX_READ_REGISTERS = 125;
    const int MAX_WRITE_REGISTERS = 123;
    const int MAX_RW_WRITE_REGISTERS = 121;

    const size_t EXCEPTION_RESPONSE_PDU_SIZE = 2;
    const size_t WRITE_RESPONSE_PDU_SIZE = 5;

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

    TModbusRegisterRange::TModbusRegisterRange(const std::list<PRegister>& regs):
        TRegisterRange(regs)
    {
        if (regs.empty()) // shouldn't happen
            throw std::runtime_error("cannot construct empty register range");

        auto it = regs.begin();
        Start = (*it)->Address;
        int end = Start + (*it)->Width();
        while (++it != regs.end()) {
            if ((*it)->Type != Type())
                throw std::runtime_error("registers of different type in the same range");
            int new_end = (*it)->Address + (*it)->Width();
            if (new_end > end)
                end = new_end;
        }
        Count = end - Start;
        if (Count > (IsSingleBitType(Type()) ? MAX_READ_BITS : MAX_READ_REGISTERS))
            throw std::runtime_error("Modbus register range too large");
    }

    void TModbusRegisterRange::MapRange(TValueCallback value_callback, TErrorCallback error_callback)
    {
        if (Error) {
            for (auto reg: RegisterList())
                error_callback(reg);
            return;
        }
        if (IsSingleBitType(Type())) {
            if (!Bits)
                throw std::runtime_error("bits not loaded");
            for (auto reg: RegisterList()) {
                int w = reg->Width();
                if (w != 1)
                    throw TSerialDeviceException(
                        "width other than 1 is not currently supported for reg type" +
                        reg->TypeName);
                if (reg->Address - Start >= Count)
                    throw std::runtime_error("address out of range");
                value_callback(reg, Bits[reg->Address - Start]);
            }
            return;
        }

        if (!Words)
            throw std::runtime_error("words not loaded");
        for (auto reg: RegisterList()) {
            int w = reg->Width();
            if (reg->Address - Start + w > Count)
                throw std::runtime_error("address out of range");
            uint64_t r = 0;
            uint16_t* data = Words + (reg->Address - Start);
            while (w--)
                r = (r << 16) + *data++;
            value_callback(reg, r);
        }
    }

    TModbusRegisterRange::~TModbusRegisterRange() {
        if (Bits)
            delete[] Bits;
        if (Words)
            delete[] Words;
    }

    uint8_t* TModbusRegisterRange::GetBits() {
        if (!IsSingleBitType(Type()))
            throw std::runtime_error("GetBits() for non-bit register");
        if (!Bits)
            Bits = new uint8_t[Count];
        return Bits;
    }

    uint16_t* TModbusRegisterRange::GetWords() {
        if (IsSingleBitType(Type()))
            throw std::runtime_error("GetWords() for non-word register");
        if (!Words)
            Words = new uint16_t[Count];
        return Words;
    }

    const uint8_t EXCEPTION_BIT = 1 << 7;

    enum ModbusError: uint8_t {
        ERR_ILLEGAL_FUNCTION                        = 0x1,
        ERR_ILLEGAL_DATA_ADDRESS                    = 0x2,
        ERR_ILLEGAL_DATA_VALUE                      = 0x3,
        ERR_SERVER_DEVICE_FAILURE                   = 0x4,
        ERR_ACKNOWLEDGE                             = 0x5,
        ERR_SERVER_DEVICE_BUSY                      = 0x6,
        ERR_MEMORY_PARITY_ERROR                     = 0x8,
        ERR_GATEWAY_PATH_UNAVAILABLE                = 0xA,
        ERR_GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND = 0xB
    };

    enum ModbusFunction: uint8_t {
        FN_READ_COILS               = 0x1,
        FN_READ_DISCRETE            = 0x2,
        FN_READ_HOLDING             = 0x3,
        FN_READ_INPUT               = 0x4,
        FN_WRITE_SINGLE_COIL        = 0x5,
        FN_WRITE_SINGLE_REGISTER    = 0x6,
        FN_WRITE_MULTIPLE_COILS     = 0xF,
        FN_WRITE_MULTIPLE_REGISTERS = 0x10,
    };

    enum class OperationType: uint8_t {
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
            return pdu[1];      // then error code in the next byte
        }
        return 0;
    }

    // choose function code for modbus request
    uint8_t GetFunctionImpl(int registerType, OperationType op, const std::string& typeName, bool many)
    {
        switch (registerType) {
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

    inline uint8_t GetFunction(PRegister reg, OperationType op)
    {
        return GetFunctionImpl(reg->Type, op, reg->TypeName, IsPacking(reg));
    }

    inline uint8_t GetFunction(PRegisterRange range, OperationType op)
    {
        return GetFunctionImpl(range->Type(), op, range->TypeName(), true);
    }

    // throws C++ exception on modbus error code
    void ThrowIfModbusException(uint8_t code)
    {
        std::string message;
        bool is_transient = true;
        switch (code) {
        case 0:
            return; // not an error
        case ERR_ILLEGAL_FUNCTION:
            message = "illegal function";
            break;
        case ERR_ILLEGAL_DATA_ADDRESS:
            message = "illegal data address";
            break;
        case ERR_ILLEGAL_DATA_VALUE:
            message = "illegal data value";
            break;
        case ERR_SERVER_DEVICE_FAILURE:
            message = "server device failure";
            break;
        case ERR_ACKNOWLEDGE:
            message = "long operation (acknowledge)";
            break;
        case ERR_SERVER_DEVICE_BUSY:
            message = "server device is busy";
            break;
        case ERR_MEMORY_PARITY_ERROR:
            message = "memory parity error";
            break;
        case ERR_GATEWAY_PATH_UNAVAILABLE:
            message = "gateway path is unavailable";
            break;
        case ERR_GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND:
            message = "gateway target device failed to respond";
            break;
        default:
            message = "invalid modbus error code (" + std::to_string(code) + ")";
            break;
        }
        if (is_transient) {
            throw TSerialDeviceTransientErrorException(message);
        } else {
            throw TSerialDevicePermanentRegisterException(message);
        }
    }

    // returns count of modbus registers needed to represent TRegister
    uint16_t GetQuantity(PRegister reg)
    {
        int w = reg->Width();

        if (IsSingleBitType(reg->Type)) {
            if (w != 1) {
                throw TSerialDeviceException("width other than 1 is not currently supported for reg type" + reg->TypeName);
            }
            return 1;
        } else {
            if (w > 4) {
                throw TSerialDeviceException("can't pack more than 4 " + reg->TypeName + "s into a single value");
            }
            return w;
        }
    }

    // returns count of modbus registers needed to represent TModbusRegisterRange
    uint16_t GetQuantity(PModbusRegisterRange range)
    {
        auto type = range->Type();

        if (!IsSingleBitType(type) && type != REG_HOLDING && type != REG_INPUT) {
            throw TSerialDeviceException("invalid register type");
        }

        return range->GetCount();
    }

    // returns number of bytes needed to hold request
    size_t InferWriteRequestPDUSize(PRegister reg)
    {
       return IsPacking(reg) ? 6 + reg->Width() * 2 : 5;
    }

    // returns number of bytes needed to hold response
    size_t InferReadResponsePDUSize(PModbusRegisterRange range)
    {
        auto count = range->GetCount();

        if (IsSingleBitType(range->Type())) {
            return 2 + std::ceil(static_cast<float>(count) / 8);    // coil values are packed into bytes as bitset
        } else {
            return 2 + count * 2;   // count is for uint16_t, we need byte count
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
    void ComposeReadRequestPDU(uint8_t* pdu, PRegister reg, int shift)
    {
        pdu[0] = GetFunction(reg, OperationType::OP_READ);
        WriteAs2Bytes(pdu + 1, reg->Address + shift);
        WriteAs2Bytes(pdu + 3, GetQuantity(reg));
    }

    void ComposeReadRequestPDU(uint8_t* pdu, PModbusRegisterRange range, int shift)
    {
        pdu[0] = GetFunction(range, OperationType::OP_READ);
        WriteAs2Bytes(pdu + 1, range->GetStart() + shift);
        WriteAs2Bytes(pdu + 3, GetQuantity(range));
    }

    // fills pdu with write request data according to Modbus specification
    void ComposeMultipleWriteRequestPDU(uint8_t* pdu, PRegister reg, uint64_t value, int shift)
    {
        pdu[0] = GetFunction(reg, OperationType::OP_WRITE);

        WriteAs2Bytes(pdu + 1, reg->Address + shift);
        WriteAs2Bytes(pdu + 3, reg->Width());

        pdu[5] = reg->Width() * 2;

        for (int p = reg->Width() - 1; p >= 0; --p) {
            WriteAs2Bytes(pdu + 6 + p * 2, value & 0xffff);
            value >>= 16;
        }
    }

    void ComposeSingleWriteRequestPDU(uint8_t* pdu, PRegister reg, uint16_t value, int shift)
    {
        if (reg->Type == REG_COIL) {
            value = value ? uint16_t(0xFF) << 8: 0x00;
        }

        pdu[0] = GetFunction(reg, OperationType::OP_WRITE);

        WriteAs2Bytes(pdu + 1, reg->Address + shift);
        WriteAs2Bytes(pdu + 3, value);
    }

    // parses modbus response and stores result
    void ParseReadResponse(const uint8_t* pdu, PModbusRegisterRange range)
    {
        ThrowIfModbusException(GetExceptionCode(pdu));

        uint8_t byte_count = pdu[1];

        auto start = pdu + 2;
        auto end = start + byte_count;
        if (IsSingleBitType(range->Type())) {
            auto destination = range->GetBits();
            auto coil_count = range->GetCount();
            while (start != end) {
                std::bitset<8> coils(*start++);
                auto coils_in_byte = std::min(coil_count, 8);
                for (int i = 0; i < coils_in_byte; ++i) {
                    destination[i] = coils[i];
                }

                coil_count -= coils_in_byte;
                destination += coils_in_byte;
            }
        } else {
            auto destination = range->GetWords();
            for (int i = 0; i < byte_count / 2; ++i) {
                destination[i] = (*start << 8) | *(start + 1);
                start += 2;
            }
        }
    }

    // checks modbus response on write
    void ParseWriteResponse(const uint8_t* pdu)
    {
        ThrowIfModbusException(GetExceptionCode(pdu));
    }

    std::list<PRegisterRange> SplitRegisterList(const std::list<PRegister> reg_list, PDeviceConfig deviceConfig, bool debug)
    {
        std::list<PRegisterRange> r;
        if (reg_list.empty())
            return r;

        std::list<PRegister> l;
        int prev_start = -1, prev_type = -1, prev_end = -1;
        std::chrono::milliseconds prev_interval;
        int max_hole = IsSingleBitType(reg_list.front()->Type) ? deviceConfig->MaxBitHole : deviceConfig->MaxRegHole;
        int max_regs;

        if (IsSingleBitType(reg_list.front()->Type)) {
            max_regs = MAX_READ_BITS;
        } else {
            if ((deviceConfig->MaxReadRegisters > 0) && (deviceConfig->MaxReadRegisters <= MAX_READ_REGISTERS)) {
                max_regs = deviceConfig->MaxReadRegisters;
            } else {
                max_regs = MAX_READ_REGISTERS;
            }
        }

        for (auto reg: reg_list) {
            int new_end = reg->Address + reg->Width();
            if (!(prev_end >= 0 &&
                reg->Type == prev_type &&
                reg->Address >= prev_end &&
                reg->Address <= prev_end + max_hole &&
                reg->PollInterval == prev_interval &&
                new_end - prev_start <= max_regs)) {
                if (!l.empty()) {
                    auto range = std::make_shared<TModbusRegisterRange>(l);
                    if (debug)
                        std::cerr << "Adding range: " << range->GetCount() << " " <<
                            range->TypeName() << "(s) @ " << range->GetStart() <<
                            " of device " << range->Device()->ToString() << std::endl;
                    r.push_back(range);
                    l.clear();
                }
                prev_start = reg->Address;
                prev_type = reg->Type;
                prev_interval = reg->PollInterval;
            }
            l.push_back(reg);
            prev_end = new_end;
        }
        if (!l.empty()) {
            auto range = std::make_shared<TModbusRegisterRange>(l);
            if (debug)
                std::cerr << "Adding range: " << range->GetCount() << " " <<
                    range->TypeName() << "(s) @ " << range->GetStart() <<
                    " of device " << range->Device()->ToString() << std::endl;
            r.push_back(range);
        }
        return r;
    }

    std::chrono::microseconds GetFrameTimeout(int baudRate)
    {
        return std::chrono::microseconds(static_cast<int64_t>(std::ceil(static_cast<double>(35000000)/baudRate)));
    }
};  // modbus protocol common utilities

namespace ModbusRTU // modbus rtu protocol utilities
{
    using TReadRequest = std::array<uint8_t, 8>;
    using TWriteRequest = std::vector<uint8_t>;

    using TReadResponse = std::vector<uint8_t>;
    using TWriteResponse = std::array<uint8_t, 8>;

    class TInvalidCRCError: public TSerialDeviceTransientErrorException
    {
    public:
        TInvalidCRCError(): TSerialDeviceTransientErrorException("invalid crc")
        {}
    };

    const size_t DATA_SIZE = 3;  // number of bytes in ADU that is not in PDU (slaveID (1b) + crc value (2b))
    const std::chrono::milliseconds FrameTimeout(500);   // libmodbus default

    // get pointer to PDU in message
    template <class T>
    inline const uint8_t* PDU(const T& msg)
    {
        return &msg[1];
    }

    template <class T>
    inline uint8_t* PDU(T& msg)
    {
        return &msg[1];
    }

    inline size_t InferWriteRequestSize(PRegister reg)
    {
        return Modbus::InferWriteRequestPDUSize(reg) + DATA_SIZE;
    }

    inline size_t InferReadResponseSize(Modbus::PModbusRegisterRange range)
    {
        return Modbus::InferReadResponsePDUSize(range) + DATA_SIZE;
    }

    TPort::TFrameCompletePred ExpectNBytes(int n)
    {
        return [n](uint8_t* buf, int size) {
            if (size < 2)
                return false;
            if (Modbus::IsException(PDU(buf)))
                return size >= static_cast<int>(Modbus::EXCEPTION_RESPONSE_PDU_SIZE + DATA_SIZE);
            return size >= n;
        };
    }

    void ComposeReadRequest(TReadRequest& req, Modbus::PModbusRegisterRange range, uint8_t slaveId, int shift)
    {
        req[0] = slaveId;
        Modbus::ComposeReadRequestPDU(PDU(req), range, shift);
        WriteAs2Bytes(&req[6], CRC16::CalculateCRC16(req.data(), 6));
    }

    void ComposeWriteRequest(TWriteRequest& req, PRegister reg, uint8_t slaveId, uint64_t value, int shift)
    {
        req.resize(InferWriteRequestSize(reg));
        req[0] = slaveId;

        if (IsPacking(reg)) {
            Modbus::ComposeMultipleWriteRequestPDU(PDU(req), reg, value, shift);
        } else {
            Modbus::ComposeSingleWriteRequestPDU(PDU(req), reg, static_cast<uint16_t>(value), shift);
        }

        WriteAs2Bytes(&req[req.size() - 2], CRC16::CalculateCRC16(req.data(), req.size() - 2));
    }

    size_t GetResponsePDUSize(const TReadResponse & res)
    {
        return Modbus::ReadResponsePDUSize(PDU(res));
    }

    size_t GetResponsePDUSize(const TWriteResponse & res)
    {
        return Modbus::WriteResponsePDUSize(PDU(res));
    }

    template <class TRequest, class TResponse>
    void CheckResponse(const TRequest & req, const TResponse & res)
    {
        auto pdu_size = GetResponsePDUSize(res);

        uint16_t crc = (res[pdu_size + 1] << 8) + res[pdu_size + 2];
        if (crc != CRC16::CalculateCRC16(res.data(), pdu_size + 1)) {
            throw TInvalidCRCError();
        }

        auto requestSlaveId = req[0];
        auto responseSlaveId = res[0];
        if (requestSlaveId != responseSlaveId) {
            throw TSerialDeviceTransientErrorException("request and response slave id mismatch");
        }

        auto requestFunctionCode = PDU(req)[0];
        auto responseFunctionCode = PDU(res)[0] & 127; // get actual function code even if exception

        if (requestFunctionCode != responseFunctionCode) {
            throw TSerialDeviceTransientErrorException("request and response function code mismatch");
        }
    }

    void WriteRegister(PPort port, uint8_t slaveId, PRegister reg, uint64_t value, int shift)
    {
        int w = reg->Width();

        if (port->Debug())
            std::cerr << "modbus: write " << w << " " << reg->TypeName << "(s) @ " << reg->Address <<
                " of device " << reg->Device()->ToString() << std::endl;
        std::string exception_message;
        try {
            TWriteRequest request;
            {   // Send request
                ComposeWriteRequest(request, reg, slaveId, value, shift);
                port->WriteBytes(request.data(), request.size());
            }

            {   // Receive response
                TWriteResponse response;
                if (port->ReadFrame(response.data(), response.size(), FrameTimeout, ExpectNBytes(response.size())) > 0) {
                    try {
                        ModbusRTU::CheckResponse(request, response);
                        Modbus::ParseWriteResponse(PDU(response));
                    } catch (const TInvalidCRCError &) {
                        try {
                            port->SkipNoise();
                        } catch (const std::exception & e) {
                            std::cerr << "SkipNoise failed: " << e.what() << std::endl;
                        }
                        throw;
                    }
                    return;
                }
            }
        } catch (TSerialDeviceTransientErrorException& e) {
            exception_message = ": ";
            exception_message += e.what();
        }

        throw TSerialDeviceTransientErrorException(
            "failed to write " + reg->TypeName +
            " @ " + std::to_string(reg->Address) + exception_message);
    }

    void ReadRegisterRange(PPort port, uint8_t slaveId, PRegisterRange range, int shift)
    {
        auto modbus_range = std::dynamic_pointer_cast<Modbus::TModbusRegisterRange>(range);
        if (!modbus_range) {
            throw std::runtime_error("modbus range expected");
        }

        if (port->Debug())
            std::cerr << "modbus: read " << modbus_range->GetCount() << " " <<
                modbus_range->TypeName() << "(s) @ " << modbus_range->GetStart() <<
                " of device " << modbus_range->Device()->ToString() << std::endl;

        if (modbus_range->Device()->DeviceConfig()->GuardInterval.count()){
            usleep(modbus_range->Device()->DeviceConfig()->GuardInterval.count());
        }

        std::string exception_message;
        try {
            TReadRequest request;

            {   // Send request
                ComposeReadRequest(request, modbus_range, slaveId, shift);
                port->WriteBytes(request.data(), request.size());
            }

            {   // Receive response
                auto byte_count = InferReadResponseSize(modbus_range);
                TReadResponse response(byte_count);

                auto rc = port->ReadFrame(response.data(), response.size(), FrameTimeout, ExpectNBytes(response.size()));
                if (rc > 0) {
                    try {
                        ModbusRTU::CheckResponse(request, response);
                        Modbus::ParseReadResponse(PDU(response), modbus_range);
                    } catch (const TInvalidCRCError &) {
                        try {
                            port->SkipNoise();
                        } catch (const std::exception & e) {
                            std::cerr << "SkipNoise failed: " << e.what() << std::endl;
                        }
                        throw;
                    }
                    modbus_range->SetError(false);
                    return;
                }
            }
        } catch (TSerialDeviceTransientErrorException& e) {
            exception_message = e.what();
        }

        modbus_range->SetError(true);
        std::ios::fmtflags f(std::cerr.flags());
        std::cerr << "ModbusRTU::ReadRegisterRange(): failed to read " << modbus_range->GetCount() << " " <<
            modbus_range->TypeName() << "(s) @ " << modbus_range->GetStart() <<
            " of device " << modbus_range->Device()->ToString();
        if (!exception_message.empty()) {
            std::cerr << ": " << exception_message;
        }
        std::cerr << std::endl;
        std::cerr.flags(f);
    }
};  // modbus rtu protocol utilities
