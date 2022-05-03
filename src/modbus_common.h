#pragma once

#include "port.h"
#include "register.h"
#include "serial_config.h"
#include "serial_device.h"
#include <array>
#include <bitset>
#include <ostream>

namespace Modbus // modbus protocol common utilities
{
    enum RegisterType
    {
        REG_HOLDING = 0, // used for 'setup' regsb
        REG_INPUT,
        REG_COIL,
        REG_DISCRETE,
        REG_HOLDING_SINGLE,
        REG_HOLDING_MULTI,
    };

    typedef std::vector<uint8_t> TRequest;
    typedef std::vector<uint8_t> TResponse;
    typedef std::map<int64_t, uint16_t> TRegisterCache;

    class IModbusTraits
    {
    public:
        virtual ~IModbusTraits() = default;

        virtual size_t GetPacketSize(size_t pduSize) const = 0;

        virtual void FinalizeRequest(TRequest& request, uint8_t slaveId) = 0;

        /**
         * @brief Read response to specified request.
         *        Throws TSerialDeviceTransientErrorException on timeout.
         *
         * @return size_t PDU size in bytes
         */
        virtual size_t ReadFrame(TPort& port,
                                 const std::chrono::milliseconds& responseTimeout,
                                 const std::chrono::milliseconds& frameTimeout,
                                 const TRequest& req,
                                 TResponse& resp) const = 0;

        virtual uint8_t* GetPDU(std::vector<uint8_t>& frame) const = 0;
        virtual const uint8_t* GetPDU(const std::vector<uint8_t>& frame) const = 0;
    };

    class TModbusRTUTraits: public IModbusTraits
    {
        const size_t DATA_SIZE = 3; // number of bytes in ADU that is not in PDU (slaveID (1b) + crc value (2b))

        TPort::TFrameCompletePred ExpectNBytes(int n) const;

    public:
        size_t GetPacketSize(size_t pduSize) const override;

        void FinalizeRequest(TRequest& request, uint8_t slaveId) override;

        size_t ReadFrame(TPort& port,
                         const std::chrono::milliseconds& responseTimeout,
                         const std::chrono::milliseconds& frameTimeout,
                         const TRequest& req,
                         TResponse& resp) const override;

        uint8_t* GetPDU(std::vector<uint8_t>& frame) const;
        const uint8_t* GetPDU(const std::vector<uint8_t>& frame) const;
    };

    class TModbusTCPTraits: public IModbusTraits
    {
        const size_t MBAP_SIZE = 7;

        std::shared_ptr<uint16_t> TransactionId;

        void SetMBAP(TRequest& req, uint16_t transactionId, size_t pduSize, uint8_t slaveId) const;
        uint16_t GetLengthFromMBAP(const TResponse& buf) const;

    public:
        TModbusTCPTraits(std::shared_ptr<uint16_t> transactionId);

        size_t GetPacketSize(size_t pduSize) const override;

        void FinalizeRequest(TRequest& request, uint8_t slaveId) override;

        size_t ReadFrame(TPort& port,
                         const std::chrono::milliseconds& responseTimeout,
                         const std::chrono::milliseconds& frameTimeout,
                         const TRequest& req,
                         TResponse& resp) const override;

        uint8_t* GetPDU(std::vector<uint8_t>& frame) const;
        const uint8_t* GetPDU(const std::vector<uint8_t>& frame) const;
    };

    class IModbusTraitsFactory
    {
    public:
        virtual ~IModbusTraitsFactory() = default;
        virtual std::unique_ptr<Modbus::IModbusTraits> GetModbusTraits(PPort port) = 0;
    };

    class TModbusTCPTraitsFactory: public IModbusTraitsFactory
    {
        std::unordered_map<PPort, std::shared_ptr<uint16_t>> TransactionIds;

    public:
        std::unique_ptr<Modbus::IModbusTraits> GetModbusTraits(PPort port) override;
    };

    class TModbusRTUTraitsFactory: public IModbusTraitsFactory
    {
    public:
        std::unique_ptr<Modbus::IModbusTraits> GetModbusTraits(PPort port) override;
    };

    PRegisterRange CreateRegisterRange();

    void WriteRegister(IModbusTraits& traits,
                       TPort& port,
                       uint8_t slaveId,
                       TRegister& reg,
                       TChannelValue value,
                       TRegisterCache& cache,
                       int shift = 0);

    void ReadRegisterRange(IModbusTraits& traits,
                           TPort& port,
                           uint8_t slaveId,
                           PRegisterRange range,
                           TRegisterCache& cache,
                           int shift = 0);

    void WriteSetupRegisters(IModbusTraits& traits,
                             TPort& port,
                             uint8_t slaveId,
                             const std::vector<PDeviceSetupItem>& setupItems,
                             TRegisterCache& cache,
                             int shift = 0);

    class TMalformedResponseError: public TSerialDeviceTransientErrorException
    {
    public:
        TMalformedResponseError(const std::string& what);
    };

} // modbus protocol common utilities
