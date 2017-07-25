#include <iostream>
#include <stdexcept>
#include <array>
#include <bitset>

#include "modbus_device.h"
#include "crc16.h"


namespace {
    const int MODBUS_MAX_READ_BITS = 2000;
    const int MODBUS_MAX_WRITE_BITS = 1968;

    const int MODBUS_MAX_READ_REGISTERS = 125;
    const int MODBUS_MAX_WRITE_REGISTERS = 123;
    const int MODBUS_MAX_RW_WRITE_REGISTERS = 121;

    // returns true if more than one modbus register is needed to represent TRegister
    inline bool IsPacking(PRegister reg)
    {
        return (reg->Type == TModbusDevice::REG_HOLDING) && (reg->Width() > 1);
    }

    static bool IsSingleBitType(int type) {
        return type == TModbusDevice::REG_COIL || type == TModbusDevice::REG_DISCRETE;
    }

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
        if (Count > (IsSingleBitType(Type()) ? MODBUS_MAX_READ_BITS : MODBUS_MAX_READ_REGISTERS))
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
};


namespace Modbus { // modbus protocol common utilities
    enum ModbusErrors: uint8_t {
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

    enum class OperationType: uint8_t {
        OP_READ = 0,
        OP_WRITE
    };

    // write 16-bit value to byte array in big-endian order
    inline void WriteAs2Bytes(uint8_t* dst, uint16_t val)
    {
        dst[0] = static_cast<uint8_t>(val >> 8);
        dst[1] = static_cast<uint8_t>(val);
    }

    // returns modbus exception code if there is any, otherwise 0
    inline uint8_t ExceptionCode(const uint8_t* pdu)
    {
        if (pdu[0] & 0x80) {    // if most significant bit set in function code,
            return pdu[1];      // then error code in the next byte
        }
        return 0;
    }

    // choose function code for modbus request
    uint8_t GetFunctionImpl(int registerType, OperationType op, const std::string& typeName, bool many)
    {
        // TBD: actually should use modbus_write_and_read_registers
        // and echo back the value that was read (must be configurable)

        switch (registerType) {
        case TModbusDevice::REG_HOLDING:
            switch (op) {
            case OperationType::OP_READ:
                return 3;
            case OperationType::OP_WRITE:
                return many ? 16 : 6;
            default:
                break;
            }
            break;
        case TModbusDevice::REG_INPUT:
            switch (op) {
            case OperationType::OP_READ:
                return 4;
            default:
                break;
            }
            break;
        case TModbusDevice::REG_COIL:
            switch (op) {
            case OperationType::OP_READ:
                return 1;
            case OperationType::OP_WRITE:
                return many ? 15 : 5;
            default:
                break;
            }
            break;
        case TModbusDevice::REG_DISCRETE:
            switch (op) {
            case OperationType::OP_READ:
                return 2;
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

    // converts modbus error code to C++ exception
    void HandleModbusException(uint8_t code)
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
            throw std::runtime_error("invalid modbus error code");
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

        if (!IsSingleBitType(type) && type != TModbusDevice::REG_HOLDING && type != TModbusDevice::REG_INPUT) {
            throw TSerialDeviceException("invalid register type");
        }

        return range->GetCount();
    }

    // returns number of bytes needed to hold request
    inline size_t InferWriteRequestPDUSize(PRegister reg)
    {
       return IsPacking(reg) ? 6 + reg->Width() * 2 : 5;
    }

    // returns number of bytes needed to hold response
    inline size_t InferReadResponsePDUSize(PModbusRegisterRange range)
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
        return pdu[0] & 0x80 ? 2 : pdu[1] + 2;
    }

    inline size_t WriteResponsePDUSize(const uint8_t* pdu)
    {
        return pdu[0] & 0x80 ? 2 : 5;
    }

    void ComposeReadRequestPDU(uint8_t* dst, PRegister reg)
    {
        dst[0] = GetFunction(reg, OperationType::OP_READ);
        WriteAs2Bytes(dst + 1, reg->Address);
        WriteAs2Bytes(dst + 3, GetQuantity(reg));
    }

    void ComposeReadRequestPDU(uint8_t* dst, PModbusRegisterRange range)
    {
        dst[0] = GetFunction(range, OperationType::OP_READ);
        WriteAs2Bytes(dst + 1, range->GetStart());
        WriteAs2Bytes(dst + 3, GetQuantity(range));
    }

    void ComposeMultipleWriteRequestPDU(uint8_t* dst, PRegister reg, uint64_t value)
    {
        dst[0] = GetFunction(reg, OperationType::OP_WRITE);

        WriteAs2Bytes(dst + 1, reg->Address);
        WriteAs2Bytes(dst + 3, reg->Width());

        dst[5] = reg->Width() * 2;

        for (int p = reg->Width() - 1; p >= 0; --p) {
            WriteAs2Bytes(dst + 6 + p * 2, value & 0xffff);
            value >>= 16;
        }
    }

    void ComposeSingleWriteRequestPDU(uint8_t* dst, PRegister reg, uint16_t value)
    {
        if (reg->Type == TModbusDevice::REG_COIL) {
            value = value ? uint16_t(0xFF) << 8: 0x00;
        }

        dst[0] = GetFunction(reg, OperationType::OP_WRITE);

        WriteAs2Bytes(dst + 1, reg->Address);
        WriteAs2Bytes(dst + 3, value);
    }
    
    // parses modbus response and stores result
    void ParseReadResponse(const uint8_t* pdu, PModbusRegisterRange range)
    {
        HandleModbusException(ExceptionCode(pdu));

        uint8_t byte_count = pdu[1];

        auto start = pdu + 2;
        auto end = start + byte_count;
        if (IsSingleBitType(range->Type())) {
            auto destination = range->GetBits();
            auto coil_count = range->GetCount();
            while (start != end) {
                std::bitset<8> coils(*start++);
                for (int i = 0; i < coil_count; ++i) {
                    destination[i] = coils[i];
                }

                if (coil_count < 8)
                    coil_count = 0;
                else
                    coil_count -= 8;
            }
        } else {
            auto destination = range->GetWords();
            for (int i = 0; i < byte_count / 2; ++i) {
                destination[i] = *start << 8 | *(start + 1);
                start += 2;
            }
        }
    }

    // checks modbus response on write
    void ParseWriteResponse(const uint8_t* pdu)
    {
        HandleModbusException(ExceptionCode(pdu));
    }
};  // modbus protocol common utilities


namespace RTU { // modbus rtu protocol utilities
    const size_t MODBUS_DATA_SIZE = 3;  // number of bytes in ADU that is not in PDU (slaveID (1b) + crc value (2b))


    // get pointer to PDU in message
    template <class T>
    inline const uint8_t* PDU(const T& msg)
    {
        return &msg[1];
    }

    // returns predicate for frame ending detection
    TAbstractSerialPort::TFrameCompletePred ExpectNBytes(int n)
    {
        return [n](uint8_t* buf, int size) {
            if (size < 2)
                return false;
            if (buf[1] & 0x80)
                return size >= 5; // exception response
            return size >= n;
        };
    }

    // parses modbus rtu response, checks crc and stores result
    void ParseReadResponse(const std::vector<uint8_t>& res, PModbusRegisterRange range)
    {
        auto pdu_size = Modbus::ReadResponsePDUSize(PDU(res));

        uint16_t crc = (res[pdu_size + 1] << 8) + res[pdu_size + 2];
        if (crc != CRC16::CalculateCRC16(res.data(), pdu_size + 1)) {
            throw TSerialDeviceTransientErrorException("invalid crc");
        }

        Modbus::ParseReadResponse(PDU(res), range);
    }

    // checks modbus rtu response on write with crc check
    void ParseWriteResponse(const std::array<uint8_t, 8>& res)
    {
        auto pdu_size = Modbus::WriteResponsePDUSize(PDU(res));

        uint16_t crc = (res[pdu_size + 1] << 8) + res[pdu_size + 2];
        if (crc != CRC16::CalculateCRC16(res.data(), pdu_size + 1)) {
            throw TSerialDeviceTransientErrorException("invalid crc");
        }

        Modbus::ParseWriteResponse(PDU(res));
    }
};  // modbus rtu protocol utilities


using namespace Modbus;

REGISTER_BASIC_INT_PROTOCOL("modbus", TModbusDevice, TRegisterTypes({
            { TModbusDevice::REG_COIL, "coil", "switch", U8 },
            { TModbusDevice::REG_DISCRETE, "discrete", "switch", U8, true },
            { TModbusDevice::REG_HOLDING, "holding", "text", U16 },
            { TModbusDevice::REG_INPUT, "input", "text", U16, true }
        }));

TModbusDevice::TModbusDevice(PDeviceConfig config, PAbstractSerialPort port, PProtocol protocol)
    : TBasicProtocolSerialDevice<TBasicProtocol<TModbusDevice>>(config, port, protocol)
{
    FrameTimeout = DeviceConfig()->FrameTimeout;

    if (auto serial_port = std::dynamic_pointer_cast<TSerialPort>(port)) {
        if (FrameTimeout.count() < 0) {
            auto microseconds = static_cast<int64_t>(std::ceil(static_cast<double>(35000000)/serial_port->GetSettings().BaudRate));
            FrameTimeout = std::chrono::microseconds(microseconds);
        }
    }
}

std::list<PRegisterRange> TModbusDevice::SplitRegisterList(const std::list<PRegister> reg_list) const
{
    std::list<PRegisterRange> r;
    if (reg_list.empty())
        return r;

    std::list<PRegister> l;
    int prev_start = -1, prev_type = -1, prev_end = -1;
    std::chrono::milliseconds prev_interval;
    int max_hole = IsSingleBitType(reg_list.front()->Type) ? DeviceConfig()->MaxBitHole : DeviceConfig()->MaxRegHole;
    int max_regs;

    if (IsSingleBitType(reg_list.front()->Type)) {
        max_regs = MODBUS_MAX_READ_BITS;
    } else {
        if ((DeviceConfig()->MaxReadRegisters > 0) && (DeviceConfig()->MaxReadRegisters <= MODBUS_MAX_READ_REGISTERS)) {
            max_regs = DeviceConfig()->MaxReadRegisters;
        } else {
            max_regs = MODBUS_MAX_READ_REGISTERS;
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
                if (Port()->Debug())
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
        if (Port()->Debug())
            std::cerr << "Adding range: " << range->GetCount() << " " <<
                range->TypeName() << "(s) @ " << range->GetStart() <<
                " of device " << range->Device()->ToString() << std::endl;
        r.push_back(range);
    }
    return r;
}

uint64_t TModbusDevice::ReadRegister(PRegister reg)
{
    throw TSerialDeviceException("modbus: single register reading is not supported");
}

void TModbusDevice::WriteRegister(PRegister reg, uint64_t value)
{
    int w = reg->Width();

    if (Port()->Debug())
        std::cerr << "modbus: write " << w << " " << reg->TypeName << "(s) @ " << reg->Address <<
            " of device " << reg->Device()->ToString() << std::endl;
    std::string exception_message;
    try
    {
        {   // Send request
            TRTUWriteRequest request;
            ComposeRTUWriteRequest(request, reg, value);
            Port()->WriteBytes(request.data(), request.size());
        }

        {   // Receive response
            std::array<uint8_t, 8> response;
            if (Port()->ReadFrame(response.data(), response.size(), FrameTimeout, RTU::ExpectNBytes(response.size())) > 0) {
                RTU::ParseWriteResponse(response);
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

void TModbusDevice::ReadRegisterRange(PRegisterRange range)
{
    auto modbus_range = std::dynamic_pointer_cast<TModbusRegisterRange>(range);
    if (Port()->Debug())
        std::cerr << "modbus: read " << modbus_range->GetCount() << " " <<
            modbus_range->TypeName() << "(s) @ " << modbus_range->GetStart() <<
            " of device " << modbus_range->Device()->ToString() << std::endl;

    std::string exception_message;
    try {
        {   // Send request
            TRTUReadRequest request;
            ComposeRTUReadRequest(request, range);
            Port()->WriteBytes(request.data(), request.size());
        }

        {   // Receive response
            auto byte_count = InferReadResponsePDUSize(modbus_range) + RTU::MODBUS_DATA_SIZE;
            std::vector<uint8_t> response(byte_count);

            auto rc = Port()->ReadFrame(response.data(), response.size(), FrameTimeout, RTU::ExpectNBytes(response.size()));
            if (rc > 0) {
                RTU::ParseReadResponse(response, modbus_range);
                modbus_range->SetError(false);
                return;
            }
        }
    } catch (TSerialDeviceTransientErrorException& e) {
        exception_message = e.what();
    }

    modbus_range->SetError(true);
    std::ios::fmtflags f(std::cerr.flags());
    std::cerr << "TModbusDevice::ReadRegisterRange(): failed to read " << modbus_range->GetCount() << " " <<
        modbus_range->TypeName() << "(s) @ " << modbus_range->GetStart() <<
        " of device " << modbus_range->Device()->ToString();
    if (!exception_message.empty()) {
        std::cerr << ": " << exception_message;
    }
    std::cerr << std::endl;
    std::cerr.flags(f);
}

void TModbusDevice::ComposeRTUReadRequest(TRTUReadRequest& req, PRegisterRange range)
{
    auto modbus_range = std::dynamic_pointer_cast<TModbusRegisterRange>(range);
    if (!modbus_range)
        throw std::runtime_error("modbus range expected");

    req[0] = SlaveId;
    ComposeReadRequestPDU(&req[1], modbus_range);
    WriteAs2Bytes(&req[6], CRC16::CalculateCRC16(req.data(), 6));
}

void TModbusDevice::ComposeRTUWriteRequest(TRTUWriteRequest& req, PRegister reg, uint64_t value)
{
    req.resize(InferWriteRequestPDUSize(reg) + RTU::MODBUS_DATA_SIZE);
    req[0] = SlaveId;

    if (IsPacking(reg)) {
        ComposeMultipleWriteRequestPDU(&req[1], reg, value);
    } else {
        ComposeSingleWriteRequestPDU(&req[1], reg, static_cast<uint16_t>(value));
    }

    WriteAs2Bytes(&req[req.size() - 2], CRC16::CalculateCRC16(req.data(), req.size() - 2));
}




