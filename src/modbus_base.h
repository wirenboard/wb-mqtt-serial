#pragma once

#include "port.h"

namespace Modbus
{
    const uint8_t EXCEPTION_BIT = 1 << 7;
    const double STANDARD_FRAME_TIMEOUT_BYTES = 3.5;
    const size_t EXCEPTION_RESPONSE_PDU_SIZE = 2;
    const size_t WRITE_RESPONSE_PDU_SIZE = 5;

    // 1 byte - function code, 2 bytes - starting register address, 2 bytes - quantity of registers
    const uint16_t READ_REQUEST_PDU_SIZE = 5;

    // 1 byte - function code, 2 bytes - register address, 2 bytes - value
    const uint16_t WRITE_SINGLE_PDU_SIZE = 5;

    // 1 byte - function code, 2 bytes - register address, 2 bytes - quantity of registers, 1 byte- byte count
    const uint16_t WRITE_MULTI_PDU_SIZE = 6;

    enum EFunction : uint8_t
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

    struct TReadResult
    {
        std::vector<uint8_t> Pdu;

        //! Time to first byte
        std::chrono::microseconds ResponseTime = std::chrono::microseconds::zero();
    };

    class IModbusTraits
    {
    public:
        virtual ~IModbusTraits() = default;

        /**
         * @brief Read response to specified request.
         *
         * @throw TSerialDeviceTransientErrorException on timeout.
         */
        virtual TReadResult Transaction(TPort& port,
                                        uint8_t slaveId,
                                        uint32_t sn,
                                        const std::vector<uint8_t>& requestPdu,
                                        size_t expectedResponsePduSize,
                                        const std::chrono::milliseconds& responseTimeout,
                                        const std::chrono::milliseconds& frameTimeout) = 0;
    };

    class TModbusRTUTraits: public IModbusTraits
    {
        const size_t DATA_SIZE = 3; // number of bytes in ADU that is not in PDU (slaveID (1b) + crc value (2b))

        bool ForceFrameTimeout;

        TPort::TFrameCompletePred ExpectNBytes(size_t n) const;
        size_t GetPacketSize(size_t pduSize) const;
        void FinalizeRequest(std::vector<uint8_t>& request, uint8_t slaveId);
        TReadFrameResult ReadFrame(TPort& port,
                                   uint8_t slaveId,
                                   const std::chrono::milliseconds& responseTimeout,
                                   const std::chrono::milliseconds& frameTimeout,
                                   std::vector<uint8_t>& response) const;

    public:
        TModbusRTUTraits(bool forceFrameTimeout = false);

        TReadResult Transaction(TPort& port,
                                uint8_t slaveId,
                                uint32_t sn,
                                const std::vector<uint8_t>& requestPdu,
                                size_t expectedResponsePduSize,
                                const std::chrono::milliseconds& responseTimeout,
                                const std::chrono::milliseconds& frameTimeout) override;
    };

    class TModbusTCPTraits: public IModbusTraits
    {
        const size_t MBAP_SIZE = 7;

        std::shared_ptr<uint16_t> TransactionId;

        void SetMBAP(std::vector<uint8_t>& req, uint16_t transactionId, size_t pduSize, uint8_t slaveId) const;
        uint16_t GetLengthFromMBAP(const std::vector<uint8_t>& buf) const;
        size_t GetPacketSize(size_t pduSize) const;
        void FinalizeRequest(std::vector<uint8_t>& request, uint8_t slaveId);

        TReadFrameResult ReadFrame(TPort& port,
                                   uint8_t slaveId,
                                   uint16_t transactionId,
                                   const std::chrono::milliseconds& responseTimeout,
                                   const std::chrono::milliseconds& frameTimeout,
                                   std::vector<uint8_t>& response) const;

    public:
        TModbusTCPTraits(std::shared_ptr<uint16_t> transactionId);

        TReadResult Transaction(TPort& port,
                                uint8_t slaveId,
                                uint32_t sn,
                                const std::vector<uint8_t>& requestPdu,
                                size_t expectedResponsePduSize,
                                const std::chrono::milliseconds& responseTimeout,
                                const std::chrono::milliseconds& frameTimeout) override;
    };

    class IModbusTraitsFactory
    {
    public:
        virtual ~IModbusTraitsFactory() = default;
        virtual std::unique_ptr<Modbus::IModbusTraits> GetModbusTraits(PPort port, bool forceFrameTimeout) = 0;
    };

    class TModbusTCPTraitsFactory: public IModbusTraitsFactory
    {
        std::unordered_map<PPort, std::shared_ptr<uint16_t>> TransactionIds;

    public:
        std::unique_ptr<Modbus::IModbusTraits> GetModbusTraits(PPort port, bool forceFrameTimeout) override;
    };

    class TModbusRTUTraitsFactory: public IModbusTraitsFactory
    {
    public:
        std::unique_ptr<Modbus::IModbusTraits> GetModbusTraits(PPort port, bool forceFrameTimeout) override;
    };

    class TErrorBase: public std::runtime_error
    {
    public:
        TErrorBase(const std::string& message);
    };

    class TMalformedResponseError: public TErrorBase
    {
    public:
        TMalformedResponseError(const std::string& what);
    };

    class TMalformedRequestError: public TErrorBase
    {
    public:
        TMalformedRequestError(const std::string& what);
    };

    class TUnexpectedResponseError: public TErrorBase
    {
    public:
        TUnexpectedResponseError(const std::string& what);
    };

    class TModbusExceptionError: public TErrorBase
    {
        uint8_t ExceptionCode;

    public:
        TModbusExceptionError(uint8_t exceptionCode);

        uint8_t GetExceptionCode() const;
    };

    bool IsException(uint8_t functionCode) noexcept;
    std::vector<uint8_t> ExtractResponseData(EFunction requestFunction, const std::vector<uint8_t>& pdu);
    size_t CalcResponsePDUSize(Modbus::EFunction function, size_t registerCount);

    bool IsSupportedFunction(uint8_t functionCode) noexcept;

    std::vector<uint8_t> MakePDU(Modbus::EFunction function,
                                 uint16_t address,
                                 uint16_t count,
                                 const std::vector<uint8_t>& data);
}
