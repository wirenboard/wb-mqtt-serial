#include "modbus_common.h"
#include "protocol_register.h"
#include "serial_device.h"
#include "ir_device_query.h"
#include "crc16.h"

#include <cmath>
#include <ostream>
#include <bitset>
#include <array>
#include <cassert>
#include <unistd.h>

using namespace std;

namespace Modbus    // modbus protocol declarations
{
    const int MAX_READ_BITS = 2000;
    const int MAX_WRITE_BITS = 1968;

    const int MAX_READ_REGISTERS = 125;
    const int MAX_WRITE_REGISTERS = 123;
    const int MAX_RW_WRITE_REGISTERS = 121;

    const size_t EXCEPTION_RESPONSE_PDU_SIZE = 2;
    const size_t WRITE_RESPONSE_PDU_SIZE = 5;

    enum ModbusError: uint8_t
    {
        ERR_NONE                                    = 0x0,
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

    struct TModbusProtocolInfo: TProtocolInfo
    {
        bool IsSingleBitType(int type) const override;
        int GetMaxReadRegisters() const override;
        int GetMaxReadBits() const override;
        int GetMaxWriteRegisters() const override;
        int GetMaxWriteBits() const override;
    };
}

namespace   // general utilities
{
    // write 16-bit value to byte array in big-endian order
    inline void WriteAs2Bytes(uint8_t* dst, uint16_t val)
    {
        dst[0] = static_cast<uint8_t>(val >> 8);
        dst[1] = static_cast<uint8_t>(val);
    }

    // returns true if multi write needs to be done
    inline bool IsPacking(const TIRDeviceQuery & query)
    {
        return (query.GetType() == Modbus::REG_HOLDING_MULTI) ||
              ((query.GetType() == Modbus::REG_HOLDING) && (query.GetCount() > 1));
    }

    inline bool IsSingleBitType(int type)
    {
        return (type == Modbus::REG_COIL) || (type == Modbus::REG_DISCRETE);
    }
}   // general utilities


namespace Modbus    // modbus protocol common utilities
{
    bool TModbusProtocolInfo::IsSingleBitType(int type) const
    {
        return ::IsSingleBitType(type);
    }

    int TModbusProtocolInfo::GetMaxReadRegisters() const
    {
        return MAX_READ_REGISTERS;
    }

    int TModbusProtocolInfo::GetMaxReadBits() const
    {
        return MAX_READ_BITS;
    }

    int TModbusProtocolInfo::GetMaxWriteRegisters() const
    {
        return MAX_WRITE_REGISTERS;
    }

    int TModbusProtocolInfo::GetMaxWriteBits() const
    {
        return MAX_WRITE_BITS;
    }

    const TProtocolInfo & GetProtocolInfo()
    {
        static TModbusProtocolInfo info;
        return info;
    }

    const uint8_t EXCEPTION_BIT = 1 << 7;

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
    uint8_t GetFunctionImpl(uint32_t type, bool isPacking, EQueryOperation op, function<const string &()> && getTypeName)
    {
        switch (type) {
        case REG_HOLDING_SINGLE:
        case REG_HOLDING_MULTI:
        case REG_HOLDING:
            switch (op) {
            case EQueryOperation::Read:
                return FN_READ_HOLDING;
            case EQueryOperation::Write:
                return isPacking ? FN_WRITE_MULTIPLE_REGISTERS : FN_WRITE_SINGLE_REGISTER;
            default:
                break;
            }
            break;
        case REG_INPUT:
            switch (op) {
            case EQueryOperation::Read:
                return FN_READ_INPUT;
            default:
                break;
            }
            break;
        case REG_COIL:
            switch (op) {
            case EQueryOperation::Read:
                return FN_READ_COILS;
            case EQueryOperation::Write:
                return isPacking ? FN_WRITE_MULTIPLE_COILS : FN_WRITE_SINGLE_COIL;
            default:
                break;
            }
            break;
        case REG_DISCRETE:
            switch (op) {
            case EQueryOperation::Read:
                return FN_READ_DISCRETE;
            default:
                break;
            }
            break;
        default:
            break;
        }

        switch (op) {
        case EQueryOperation::Read:
            cerr << "reading of " << getTypeName() << " is not implemented" << endl;
            assert(false);
        case EQueryOperation::Write:
            cerr << "writing to " << getTypeName() << " is not implemented" << endl;
            assert(false);
        default:
            cerr << "wrong operation code: " << to_string((int)op) << endl;
            assert(false);
        }
    }

    uint8_t GetFunction(const TIRDeviceQuery & query)
    {
        return GetFunctionImpl(query.GetType(), IsPacking(query), query.Operation, [&]{ return query.GetTypeName(); });
    }

    uint8_t GetFunction(const TProtocolRegister & protocolRegister, EQueryOperation op)
    {
        return GetFunctionImpl(protocolRegister.Type, false, op, [&]{ return protocolRegister.GetTypeName(); });
    }

    // throws C++ exception on modbus error code
    void ThrowIfModbusException(uint8_t code)
    {
        string message;
        bool is_transient = true;
        switch (code) {
        case 0:
            return; // not an error
        case ERR_ILLEGAL_FUNCTION:
            message = "illegal function";
            is_transient = false;
            break;
        case ERR_ILLEGAL_DATA_ADDRESS:
            message = "illegal data address";
            is_transient = false;
            break;
        case ERR_ILLEGAL_DATA_VALUE:
            message = "illegal data value";
            is_transient = false;
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
            message = "invalid modbus error code (" + to_string(code) + ")";
            break;
        }
        if (is_transient) {
            throw TSerialDeviceTransientErrorException(message);
        } else {
            throw TSerialDevicePermanentErrorException(message);
        }
    }

    inline size_t GetByteCount(const TIRDeviceQuery & query)
    {
        if (IsSingleBitType(query.GetType())) {
            return BitCountToByteCount(query.GetCount());    // coil values are packed into bytes as bitset
        } else {
            return query.GetCount() * 2;   // count is for uint16_t, we need byte count
        }
    }

    // returns number of bytes needed to hold request
    size_t InferWriteRequestPDUSize(const TIRDeviceQuery & query)
    {
       return IsPacking(query) ? 6 + GetByteCount(query) : 5;
    }

    // returns number of requests needed to write register
    size_t InferWriteRequestsCount(const TIRDeviceQuery & query)
    {
       return IsPacking(query) ? 1 : query.GetCount();
    }

    // returns number of bytes needed to hold response
    size_t InferReadResponsePDUSize(const TIRDeviceQuery & query)
    {
        return 2 + GetByteCount(query);
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
    void ComposeReadRequestPDU(uint8_t* pdu, const TIRDeviceQuery & query, int shift)
    {
        pdu[0] = GetFunction(query);
        WriteAs2Bytes(pdu + 1, query.GetStart() + shift);
        WriteAs2Bytes(pdu + 3, query.GetCount());
    }

    // fills pdu with write request data according to Modbus specification
    void ComposeMultipleWriteRequestPDU(uint8_t* pdu, const TIRDeviceValueQuery & query, int shift)
    {
        pdu[0] = GetFunction(query);

        WriteAs2Bytes(pdu + 1, query.GetStart() + shift);
        WriteAs2Bytes(pdu + 3, query.GetCount());

        auto byteCount = GetByteCount(query);

        pdu[5] = byteCount;

        if (IsSingleBitType(query.GetType())) {
            const auto bitCount = min(query.GetCount(), 8u);
            const auto & coilValues = query.GetValues<uint8_t>(); // it is actually values of individual coils, bool is not used to avoid possible bit specialization of vector

            for (uint32_t iByte = 0; iByte < byteCount; ++iByte) {
                bitset<8> coils;
                for (uint32_t iBit = iByte * 8; iBit < bitCount; ++iBit) {
                    coils[iBit] = coilValues[iBit];
                }
                pdu[6 + iByte] = static_cast<uint8_t>(coils.to_ulong());
            }
        } else {
            const auto & values = query.GetValues<uint16_t>();

            for (uint32_t i = 0; i < values.size(); ++i) {
                WriteAs2Bytes(pdu + 6 + i * 2, values[i]);
            }
        }
    }

    void ComposeSingleWriteRequestPDU(uint8_t* pdu, const TProtocolRegister & protocolRegister, uint16_t value, int shift)
    {
        if (protocolRegister.Type == REG_COIL) {
            value = value ? uint16_t(0xFF) << 8: 0x00;
        }

        pdu[0] = GetFunction(protocolRegister, EQueryOperation::Write);

        assert(pdu[0] == FN_WRITE_SINGLE_COIL || pdu[0] == FN_WRITE_SINGLE_REGISTER);

        WriteAs2Bytes(pdu + 1, protocolRegister.Address + shift);
        WriteAs2Bytes(pdu + 3, value);
    }

    void CheckWriteResponse(const uint8_t* reqPDU, const uint8_t* resPDU)
    {
        uint16_t reqAddress = (reqPDU[1] << 8) | reqPDU[2];
        uint16_t resAddress = (resPDU[1] << 8) | resPDU[2];

        if (reqAddress != resAddress) {
            throw TSerialDeviceTransientErrorException("request and response starting address mismatch. request: " + to_string(reqAddress) + "; response: " + to_string(resAddress));
        }
    }

    void CheckReadResponse(const uint8_t* pdu, const TIRDeviceQuery & query)
    {
        // byte count check
        {
            auto actualByteCount   = pdu[1];
            auto expectedByteCount = GetByteCount(query);

            if (actualByteCount != expectedByteCount) {
                throw TSerialDeviceTransientErrorException("unexpected byte count value: expected " + to_string(expectedByteCount) + " got " + to_string(actualByteCount));
            }
        }
    }

    // parses modbus response and stores result
    void ParseReadResponse(const uint8_t* pdu, const TIRDeviceQuery & query)
    {
        auto exception_code = GetExceptionCode(pdu);
        ThrowIfModbusException(exception_code);

        CheckReadResponse(pdu, query);

        uint8_t byteCount = pdu[1];

        auto bytes = pdu + 2;

        if (IsSingleBitType(query.GetType())) {
            assert(byteCount == GetByteCount(query));

            vector<uint8_t> values(query.GetCount());

            for (uint32_t iByte = 0; iByte < byteCount; ++iByte) {
                bitset<8> coils(bytes[iByte]);
                auto bitOffset = iByte * 8;
                const auto bitCount = min(query.GetCount() - bitOffset, 8u);

                for (uint8_t iBit = 0; iBit < bitCount; ++iBit) {
                    values[bitOffset + iBit] = coils[iBit];
                }
            }

            query.FinalizeRead(values);

        } else {
            assert(byteCount % 2 == 0);
            assert(byteCount / 2 == query.GetCount());

            vector<uint16_t> values(query.GetCount());

            for (uint32_t iByte = 0, i = 0; iByte < byteCount; iByte+=2) {
                values[i++] = (bytes[iByte] << 8) | bytes[iByte + 1];
            }

            query.FinalizeRead(values);
        }
    }

    // checks modbus response on write
    void ParseWriteResponse(const uint8_t* reqPDU, const uint8_t* resPDU)
    {
        ThrowIfModbusException(GetExceptionCode(resPDU));

        CheckWriteResponse(reqPDU, resPDU);
    }

    chrono::microseconds GetFrameTimeout(int baudRate)
    {
        return chrono::microseconds(static_cast<int64_t>(ceil(static_cast<double>(35000000)/baudRate)));
    }
};  // modbus protocol common utilities

namespace ModbusRTU // modbus rtu protocol utilities
{
    using TReadRequest = array<uint8_t, 8>;
    using TWriteRequest = vector<uint8_t>;

    using TReadResponse = vector<uint8_t>;
    using TWriteResponse = array<uint8_t, 8>;

    class TInvalidCRCError: public TSerialDeviceTransientErrorException
    {
    public:
        TInvalidCRCError(): TSerialDeviceTransientErrorException("invalid crc")
        {}
    };

    // Mostly occures when frame was read shifted or there is trash on bus
    class TMalformedResponseError: public TSerialDeviceTransientErrorException
    {
    public:
        TMalformedResponseError(const string & what): TSerialDeviceTransientErrorException("malformed response: " + what)
        {}
    };

    const size_t DATA_SIZE = 3;  // number of bytes in ADU that is not in PDU (slaveID (1b) + crc value (2b))
    const chrono::milliseconds FrameTimeout(500);   // libmodbus default

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

    inline size_t InferWriteRequestSize(const TIRDeviceQuery & query)
    {
        return Modbus::InferWriteRequestPDUSize(query) + DATA_SIZE;
    }

    inline size_t InferReadResponseSize(const TIRDeviceQuery & query)
    {
        return Modbus::InferReadResponsePDUSize(query) + DATA_SIZE;
    }

    template <class T>
    inline void SignCRC16(T & msg)
    {
        WriteAs2Bytes(&msg[msg.size() - 2], CRC16::CalculateCRC16(msg.data(), msg.size() - 2));
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

    void ComposeReadRequest(TReadRequest & request, const TIRDeviceQuery & query, uint8_t slaveId, int shift)
    {
        request[0] = slaveId;
        Modbus::ComposeReadRequestPDU(PDU(request), query, shift);
        SignCRC16(request);
    }

    void ComposeWriteRequests(vector<TWriteRequest> & requests, const TIRDeviceValueQuery & query, uint8_t slaveId, int shift)
    {
        requests.reserve(Modbus::InferWriteRequestsCount(query));

        if (IsPacking(query)) {
            requests.emplace_back(InferWriteRequestSize(query));
            auto & request = requests.back();

            request[0] = slaveId;

            Modbus::ComposeMultipleWriteRequestPDU(PDU(request), query, shift);
            SignCRC16(request);
        } else {
            const auto & valueQuery = query.As<TIRDeviceValueQuery>();

            valueQuery.IterRegisterValues([&](const TProtocolRegister & protocolRegister, uint64_t value) {
                requests.emplace_back(InferWriteRequestSize(query));
                auto & request = requests.back();

                request[0] = slaveId;

                Modbus::ComposeSingleWriteRequestPDU(PDU(request), protocolRegister, value, shift);
                SignCRC16(request);
            });
        }
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

        if (pdu_size > (res.size() - 3)) {
            throw TMalformedResponseError("invalid data size");
        }

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

    void Read(const PPort & port, uint8_t slaveId, const TIRDeviceQuery & query, int shift)
    {
        auto config = query.GetDevice()->DeviceConfig();

        if (port->Debug())
            cerr << "modbus: read " << query.Describe() << endl;

        if (config->GuardInterval.count()){
            port->Sleep(config->GuardInterval);
        }

        string exception_message;
        TReadRequest request;

        {   // Send request
            ComposeReadRequest(request, query, slaveId, shift);
            port->WriteBytes(request.data(), request.size());
        }

        {   // Receive response
            auto byte_count = InferReadResponseSize(query);
            TReadResponse response(byte_count);
            auto frame_timeout = config->FrameTimeout.count() < 0 ? FrameTimeout: config->FrameTimeout;

            auto rc = port->ReadFrame(response.data(), response.size(), frame_timeout, ExpectNBytes(response.size()));
            if (rc > 0) {
                try {
                    ModbusRTU::CheckResponse(request, response);
                    Modbus::ParseReadResponse(PDU(response), query);
                } catch (const TInvalidCRCError &) {
                    try {
                        port->SkipNoise();
                    } catch (const exception & e) {
                        cerr << "SkipNoise failed: " << e.what() << endl;
                    }
                    throw;
                } catch (const TMalformedResponseError &) {
                    try {
                        port->SkipNoise();
                    } catch (const exception & e) {
                        cerr << "SkipNoise failed: " << e.what() << endl;
                    }
                    throw;
                }
                return;
            }
        }
    }

    void Write(const PPort & port, uint8_t slaveId, const TIRDeviceValueQuery & query, int shift)
    {
        auto config = query.GetDevice()->DeviceConfig();

        if (port->Debug())
            cerr << "modbus: write query " << query.Describe() << " of device " << query.GetDevice()->ToString() << endl;

        string exception_message;
        vector<TWriteRequest> requests;
        ComposeWriteRequests(requests, query, slaveId, shift);

        size_t writtenCount = 0;
        for (const auto & request: requests) {
            // Send request
            if (config->GuardInterval.count()) {
                port->Sleep(config->GuardInterval);
            }

            port->WriteBytes(request.data(), request.size());

            {   // Receive response
                TWriteResponse response;
                auto frame_timeout = config->FrameTimeout.count() < 0 ? FrameTimeout: config->FrameTimeout;

                if (port->ReadFrame(response.data(), response.size(), frame_timeout, ExpectNBytes(response.size())) > 0) {
                    try {
                        ModbusRTU::CheckResponse(request, response);
                        Modbus::ParseWriteResponse(PDU(request), PDU(response));
                        ++writtenCount; // no throw - successful write
                    } catch (const TInvalidCRCError &) {
                        try {
                            port->SkipNoise();
                        } catch (const exception & e) {
                            cerr << "SkipNoise failed: " << e.what() << endl;
                        }
                        throw;
                    } catch (const TMalformedResponseError &) {
                        try {
                            port->SkipNoise();
                        } catch (const exception & e) {
                            cerr << "SkipNoise failed: " << e.what() << endl;
                        }
                        throw;
                    }
                } else {
                    break;
                }
            }
        }

        if (writtenCount == requests.size()) {
            query.FinalizeWrite();
        }

        return;
    }
};  // modbus rtu protocol utilities
