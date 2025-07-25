#include "modbus_base.h"

#include "crc16.h"
#include "serial_exc.h"

using namespace std;

namespace
{
    const size_t CRC_SIZE = 2;

    std::string GetModbusExceptionMessage(uint8_t code)
    {
        if (code == 0) {
            return std::string();
        }
        // clang-format off
        const char* errs[] =
            { "illegal function",                         // 0x01
              "illegal data address",                     // 0x02
              "illegal data value",                       // 0x03
              "server device failure",                    // 0x04
              "long operation (acknowledge)",             // 0x05
              "server device is busy",                    // 0x06
              "",                                         // 0x07
              "memory parity error",                      // 0x08
              "",                                         // 0x09
              "gateway path is unavailable",              // 0x0A
              "gateway target device failed to respond"   // 0x0B
            };
        // clang-format on
        --code;
        if (code < sizeof(errs) / sizeof(char*)) {
            return errs[code];
        }
        return "invalid modbus error code (" + std::to_string(code + 1) + ")";
    }

    // throws C++ exception on modbus error code
    void ThrowIfModbusException(uint8_t code)
    {
        if (code == 0) {
            return;
        }
        throw Modbus::TModbusExceptionError(code);
    }

    bool IsWriteFunction(Modbus::EFunction function)
    {
        return function == Modbus::EFunction::FN_WRITE_MULTIPLE_COILS ||
               function == Modbus::EFunction::FN_WRITE_MULTIPLE_REGISTERS ||
               function == Modbus::EFunction::FN_WRITE_SINGLE_COIL ||
               function == Modbus::EFunction::FN_WRITE_SINGLE_REGISTER;
    }

    bool IsReadFunction(Modbus::EFunction function)
    {
        return function == Modbus::EFunction::FN_READ_COILS || function == Modbus::EFunction::FN_READ_DISCRETE ||
               function == Modbus::EFunction::FN_READ_HOLDING || function == Modbus::EFunction::FN_READ_INPUT;
    }

    bool IsReadWriteFunction(Modbus::EFunction function)
    {
        return function == Modbus::EFunction::FN_READ_WRITE_MULTIPLE_REGISTERS;
    }

    bool IsSingleBitFunction(Modbus::EFunction function)
    {
        return function == Modbus::EFunction::FN_READ_COILS || function == Modbus::EFunction::FN_READ_DISCRETE ||
               function == Modbus::EFunction::FN_WRITE_SINGLE_COIL ||
               function == Modbus::EFunction::FN_WRITE_MULTIPLE_COILS;
    }

    void WriteAs2Bytes(uint8_t* dst, uint16_t val)
    {
        dst[0] = static_cast<uint8_t>(val >> 8);
        dst[1] = static_cast<uint8_t>(val);
    }

    uint16_t GetCoilsByteSize(uint16_t count)
    {
        return count / 8 + (count % 8 != 0 ? 1 : 0);
    }

    std::vector<uint8_t> MakeReadRequestPDU(Modbus::EFunction function, uint16_t address, uint16_t count)
    {
        std::vector<uint8_t> res(5);
        res[0] = function;
        WriteAs2Bytes(res.data() + 1, address);
        WriteAs2Bytes(res.data() + 3, count);
        return res;
    }

    std::vector<uint8_t> MakeWriteSingleRequestPDU(Modbus::EFunction function,
                                                   uint16_t address,
                                                   const std::vector<uint8_t>& data)
    {
        if (data.size() != 2) {
            throw Modbus::TMalformedRequestError("data size " + std::to_string(data.size()) +
                                                 " doesn't match function code " + std::to_string(function));
        }
        std::vector<uint8_t> res(5);
        res[0] = function;
        WriteAs2Bytes(res.data() + 1, address);
        res[3] = data[0];
        res[4] = data[1];
        return res;
    }

    std::vector<uint8_t> MakeWriteMultipleRequestPDU(Modbus::EFunction function,
                                                     uint16_t address,
                                                     uint16_t count,
                                                     const std::vector<uint8_t>& data)
    {
        if ((function == Modbus::EFunction::FN_WRITE_MULTIPLE_COILS && data.size() != GetCoilsByteSize(count)) ||
            (function == Modbus::EFunction::FN_WRITE_MULTIPLE_REGISTERS && data.size() != count * 2))
        {
            throw Modbus::TMalformedRequestError("data size " + std::to_string(data.size()) +
                                                 " doesn't match function code " + std::to_string(function));
        }
        std::vector<uint8_t> res(6 + data.size());
        res[0] = function;
        WriteAs2Bytes(res.data() + 1, address);
        WriteAs2Bytes(res.data() + 3, count);
        res[5] = data.size();
        std::copy(data.begin(), data.end(), res.begin() + 6);
        return res;
    }

    // get actual function code even if exception
    uint8_t GetFunctionCode(uint8_t functionCodeByte)
    {
        return functionCodeByte & 127;
    }

    Modbus::EFunction GetFunction(uint8_t functionCode)
    {
        if (Modbus::IsSupportedFunction(functionCode)) {
            return static_cast<Modbus::EFunction>(functionCode);
        }
        throw Modbus::TUnexpectedResponseError("unknown modbus function code: " + to_string(functionCode));
    }
}

Modbus::IModbusTraits::IModbusTraits(bool forceFrameTimeout): ForceFrameTimeout(forceFrameTimeout)
{}

bool Modbus::IModbusTraits::GetForceFrameTimeout()
{
    return ForceFrameTimeout;
}

// TModbusRTUTraits

Modbus::TModbusRTUTraits::TModbusRTUTraits(bool forceFrameTimeout): IModbusTraits(forceFrameTimeout)
{}

TPort::TFrameCompletePred Modbus::TModbusRTUTraits::ExpectNBytes(size_t n) const
{
    return [=](uint8_t* buf, size_t size) {
        if (size < 2)
            return false;
        if (IsException(buf[1])) // GetPDU
            return size >= EXCEPTION_RESPONSE_PDU_SIZE + DATA_SIZE;
        return size >= n;
    };
}

size_t Modbus::TModbusRTUTraits::GetPacketSize(size_t pduSize) const
{
    return DATA_SIZE + pduSize;
}

void Modbus::TModbusRTUTraits::FinalizeRequest(std::vector<uint8_t>& request, uint8_t slaveId)
{
    request[0] = slaveId;
    WriteAs2Bytes(&request[request.size() - 2], CRC16::CalculateCRC16(request.data(), request.size() - 2));
}

TReadFrameResult Modbus::TModbusRTUTraits::ReadFrame(TPort& port,
                                                     uint8_t slaveId,
                                                     const std::chrono::milliseconds& responseTimeout,
                                                     const std::chrono::milliseconds& frameTimeout,
                                                     std::vector<uint8_t>& response) const
{
    auto rc = port.ReadFrame(response.data(),
                             response.size(),
                             responseTimeout + frameTimeout,
                             frameTimeout,
                             ExpectNBytes(response.size()));
    // RTU response should be at least 3 bytes: 1 byte slave_id, 2 bytes CRC
    if (rc.Count < DATA_SIZE) {
        throw Modbus::TMalformedResponseError("invalid data size");
    }

    uint16_t crc = (response[rc.Count - 2] << 8) + response[rc.Count - 1];
    if (crc != CRC16::CalculateCRC16(response.data(), rc.Count - 2)) {
        throw Modbus::TMalformedResponseError("invalid crc");
    }

    if (ForceFrameTimeout) {
        std::array<uint8_t, 256> buf;
        try {
            port.ReadFrame(buf.data(), buf.size(), frameTimeout, frameTimeout);
        } catch (const TResponseTimeoutException& e) {
            // No extra data
        }
    }

    auto responseSlaveId = response[0];
    if (slaveId != responseSlaveId) {
        throw Modbus::TUnexpectedResponseError("request and response slave id mismatch");
    }
    return rc;
}

Modbus::TReadResult Modbus::TModbusRTUTraits::Transaction(TPort& port,
                                                          uint8_t slaveId,
                                                          const std::vector<uint8_t>& requestPdu,
                                                          size_t expectedResponsePduSize,
                                                          const std::chrono::milliseconds& responseTimeout,
                                                          const std::chrono::milliseconds& frameTimeout)
{
    std::vector<uint8_t> request(GetPacketSize(requestPdu.size()));
    std::copy(requestPdu.begin(), requestPdu.end(), request.begin() + 1);
    FinalizeRequest(request, slaveId);

    port.WriteBytes(request.data(), request.size());

    std::vector<uint8_t> response(GetPacketSize(expectedResponsePduSize));

    auto readRes = ReadFrame(port, slaveId, responseTimeout, frameTimeout, response);

    TReadResult res;
    res.ResponseTime = readRes.ResponseTime;
    res.Pdu.assign(response.begin() + 1, response.begin() + (readRes.Count - CRC_SIZE));
    return res;
}

// TModbusTCPTraits

std::mutex Modbus::TModbusTCPTraits::TransactionIdMutex;
std::unordered_map<std::string, uint16_t> Modbus::TModbusTCPTraits::TransactionIds;

uint16_t Modbus::TModbusTCPTraits::GetTransactionId(TPort& port)
{
    auto portDescription = port.GetDescription(false);
    std::unique_lock lk(TransactionIdMutex);
    try {
        return ++TransactionIds.at(portDescription);
    } catch (const std::out_of_range&) {
        TransactionIds.emplace(portDescription, 1);
        return 1;
    }
}

void Modbus::TModbusTCPTraits::ResetTransactionId(TPort& port)
{
    auto portDescription = port.GetDescription(false);
    std::unique_lock lk(TransactionIdMutex);
    TransactionIds.erase(portDescription);
}

Modbus::TModbusTCPTraits::TModbusTCPTraits()
{}

void Modbus::TModbusTCPTraits::SetMBAP(std::vector<uint8_t>& req,
                                       uint16_t transactionId,
                                       size_t pduSize,
                                       uint8_t slaveId) const
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

uint16_t Modbus::TModbusTCPTraits::GetLengthFromMBAP(const std::vector<uint8_t>& buf) const
{
    uint16_t l = buf[4];
    l <<= 8;
    l += buf[5];
    return l;
}

size_t Modbus::TModbusTCPTraits::GetPacketSize(size_t pduSize) const
{
    return MBAP_SIZE + pduSize;
}

void Modbus::TModbusTCPTraits::FinalizeRequest(std::vector<uint8_t>& request, uint8_t slaveId, uint16_t transactionId)
{
    SetMBAP(request, transactionId, request.size() - MBAP_SIZE, slaveId);
}

TReadFrameResult Modbus::TModbusTCPTraits::ReadFrame(TPort& port,
                                                     uint8_t slaveId,
                                                     uint16_t transactionId,
                                                     const std::chrono::milliseconds& responseTimeout,
                                                     const std::chrono::milliseconds& frameTimeout,
                                                     std::vector<uint8_t>& response) const
{
    auto startTime = chrono::steady_clock::now();
    while (chrono::duration_cast<chrono::milliseconds>(chrono::steady_clock::now() - startTime) <
           responseTimeout + frameTimeout)
    {
        if (response.size() < MBAP_SIZE) {
            response.resize(MBAP_SIZE);
        }
        auto rc = port.ReadFrame(response.data(), MBAP_SIZE, responseTimeout + frameTimeout, frameTimeout);

        if (rc.Count < MBAP_SIZE) {
            throw Modbus::TMalformedResponseError("Can't read full MBAP");
        }

        auto len = GetLengthFromMBAP(response);
        // MBAP length should be at least 1 byte for unit identifier
        if (len == 0) {
            throw Modbus::TMalformedResponseError("Wrong MBAP length value: 0");
        }
        --len; // length includes one byte of unit identifier which is already in buffer

        if (len + MBAP_SIZE > response.size()) {
            response.resize(len + MBAP_SIZE);
        }

        rc = port.ReadFrame(response.data() + MBAP_SIZE, len, frameTimeout, frameTimeout);
        if (rc.Count != len) {
            throw Modbus::TMalformedResponseError("Wrong PDU size: " + to_string(rc.Count) + ", expected " +
                                                  to_string(len));
        }
        rc.Count += MBAP_SIZE;

        // check transaction id
        if (((transactionId >> 8) & 0xFF) == response[0] && (transactionId & 0xFF) == response[1]) {
            // check unit identifier
            if (slaveId != response[6]) {
                throw Modbus::TUnexpectedResponseError("request and response unit identifier mismatch");
            }
            return rc;
        }
    }
    throw TResponseTimeoutException();
}

Modbus::TReadResult Modbus::TModbusTCPTraits::Transaction(TPort& port,
                                                          uint8_t slaveId,
                                                          const std::vector<uint8_t>& requestPdu,
                                                          size_t expectedResponsePduSize,
                                                          const std::chrono::milliseconds& responseTimeout,
                                                          const std::chrono::milliseconds& frameTimeout)
{
    auto transactionId = GetTransactionId(port);
    std::vector<uint8_t> request(GetPacketSize(requestPdu.size()));
    std::copy(requestPdu.begin(), requestPdu.end(), request.begin() + MBAP_SIZE);
    FinalizeRequest(request, slaveId, transactionId);

    port.WriteBytes(request.data(), request.size());

    std::vector<uint8_t> response(GetPacketSize(expectedResponsePduSize));

    auto readRes = ReadFrame(port, slaveId, transactionId, responseTimeout, frameTimeout, response);

    TReadResult res;
    res.ResponseTime = readRes.ResponseTime;
    res.Pdu.assign(response.begin() + MBAP_SIZE, response.begin() + readRes.Count);
    return res;
}

std::unique_ptr<Modbus::IModbusTraits> Modbus::TModbusRTUTraitsFactory::GetModbusTraits(bool forceFrameTimeout)
{
    return std::make_unique<Modbus::TModbusRTUTraits>(forceFrameTimeout);
}

std::unique_ptr<Modbus::IModbusTraits> Modbus::TModbusTCPTraitsFactory::GetModbusTraits(bool forceFrameTimeout)
{
    return std::make_unique<Modbus::TModbusTCPTraits>();
}

bool Modbus::IsException(uint8_t functionCode) noexcept
{
    return functionCode & EXCEPTION_BIT;
}

Modbus::TErrorBase::TErrorBase(const std::string& what): std::runtime_error(what)
{}

Modbus::TMalformedResponseError::TMalformedResponseError(const std::string& what)
    : Modbus::TErrorBase("malformed response: " + what)
{}

Modbus::TMalformedRequestError::TMalformedRequestError(const std::string& what)
    : Modbus::TErrorBase("malformed request: " + what)
{}

Modbus::TUnexpectedResponseError::TUnexpectedResponseError(const std::string& what): Modbus::TErrorBase(what)
{}

size_t Modbus::CalcResponsePDUSize(Modbus::EFunction function, size_t registerCount)
{
    if (IsWriteFunction(function)) {
        return WRITE_RESPONSE_PDU_SIZE;
    }
    if (IsSingleBitFunction(function)) {
        return 2 + GetCoilsByteSize(registerCount);
    }
    return 2 + registerCount * 2;
}

std::vector<uint8_t> Modbus::ExtractResponseData(EFunction requestFunction, const std::vector<uint8_t>& pdu)
{
    if (pdu.size() < 2) {
        throw Modbus::TMalformedResponseError("PDU is too small");
    }

    auto functionCode = GetFunctionCode(pdu[0]);
    if (requestFunction != functionCode) {
        throw Modbus::TUnexpectedResponseError("request and response function code mismatch");
    }

    if (IsException(pdu[0])) {
        ThrowIfModbusException(pdu[1]);
    }

    auto function = GetFunction(functionCode);
    if (IsReadFunction(function) || IsReadWriteFunction(function)) {
        size_t byteCount = pdu[1];
        if (pdu.size() != byteCount + 2) {
            throw Modbus::TMalformedResponseError("invalid read response byte count: " + std::to_string(byteCount) +
                                                  ", got " + std::to_string(pdu.size() - 2));
        }
        return std::vector<uint8_t>(pdu.begin() + 2, pdu.end());
    }

    if (IsWriteFunction(function)) {
        if (WRITE_RESPONSE_PDU_SIZE != pdu.size()) {
            throw Modbus::TMalformedResponseError("invalid write response PDU size: " + std::to_string(pdu.size()) +
                                                  ", expected " + std::to_string(WRITE_RESPONSE_PDU_SIZE));
        }
    }

    return std::vector<uint8_t>();
}

bool Modbus::IsSupportedFunction(uint8_t functionCode) noexcept
{
    auto function = static_cast<Modbus::EFunction>(functionCode);
    return IsReadFunction(function) || IsWriteFunction(function) || IsReadWriteFunction(function);
}

std::vector<uint8_t> Modbus::MakePDU(Modbus::EFunction function,
                                     uint16_t address,
                                     uint16_t count,
                                     const std::vector<uint8_t>& data)
{
    if (IsReadFunction(function)) {
        return MakeReadRequestPDU(function, address, count);
    }

    if (function == Modbus::EFunction::FN_WRITE_SINGLE_COIL || function == Modbus::EFunction::FN_WRITE_SINGLE_REGISTER)
    {
        return MakeWriteSingleRequestPDU(function, address, data);
    }

    if (function == Modbus::EFunction::FN_WRITE_MULTIPLE_REGISTERS ||
        function == Modbus::EFunction::FN_WRITE_MULTIPLE_COILS)
    {
        return MakeWriteMultipleRequestPDU(function, address, count, data);
    }

    return std::vector<uint8_t>();
}

std::vector<uint8_t> Modbus::MakePDU(Modbus::EFunction function,
                                     uint16_t address,
                                     uint16_t count,
                                     uint16_t writeAddress,
                                     uint16_t writeCount,
                                     const std::vector<uint8_t>& data)
{
    if (IsReadWriteFunction(function)) {
        if (data.size() != writeCount * 2) {
            throw Modbus::TMalformedRequestError("data size " + std::to_string(data.size()) +
                                                 " doesn't match function code 23");
        }
        std::vector<uint8_t> res(10 + data.size());
        res[0] = Modbus::EFunction::FN_READ_WRITE_MULTIPLE_REGISTERS;
        WriteAs2Bytes(res.data() + 1, address);
        WriteAs2Bytes(res.data() + 3, count);
        WriteAs2Bytes(res.data() + 5, writeAddress);
        WriteAs2Bytes(res.data() + 7, writeCount);
        res[9] = data.size();
        std::copy(data.begin(), data.end(), res.begin() + 10);
        return res;
    }

    return MakePDU(function, address, count, data);
}

Modbus::TModbusExceptionError::TModbusExceptionError(uint8_t exceptionCode)
    : Modbus::TErrorBase(GetModbusExceptionMessage(exceptionCode)),
      ExceptionCode(exceptionCode)
{}

uint8_t Modbus::TModbusExceptionError::GetExceptionCode() const
{
    return ExceptionCode;
}
