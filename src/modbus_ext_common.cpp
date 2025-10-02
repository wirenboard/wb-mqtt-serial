#include "modbus_ext_common.h"

#include "bin_utils.h"
#include "crc16.h"
#include "log.h"
#include "serial_exc.h"
#include <algorithm>
#include <stddef.h>
#include <stdint.h>
#include <vector>

using namespace BinUtils;
using namespace std::chrono;
using namespace std::chrono_literals;

#define LOG(logger) logger.Log() << "[modbus-ext] "

namespace ModbusExt // modbus extension protocol declarations
{
    const uint8_t BROADCAST_ADDRESS = 0xFD;

    // Function codes
    const uint8_t START_SCAN_COMMAND = 0x01;
    const uint8_t CONTINUE_SCAN_COMMAND = 0x02;
    const uint8_t DEVICE_FOUND_RESPONSE_SCAN_COMMAND = 0x03;
    const uint8_t NO_MORE_DEVICES_RESPONSE_SCAN_COMMAND = 0x04;
    const uint8_t MODBUS_STANDARD_REQUEST_COMMAND = 0x08;
    const uint8_t MODBUS_STANDARD_RESPONSE_COMMAND = 0x09;
    const uint8_t EVENTS_REQUEST_COMMAND = 0x10;
    const uint8_t HAS_EVENTS_RESPONSE_COMMAND = 0x11;
    const uint8_t NO_EVENTS_RESPONSE_COMMAND = 0x12;
    const uint8_t ENABLE_EVENTS_COMMAND = 0x18;

    const size_t CRC_SIZE = 2;
    const size_t RTU_MAX_PACKET_SIZE = 256;
    const size_t RTU_HEADER_SIZE = 1;

    // PDU
    const size_t PDU_MODBUS_STANDARD_COMMAND_HEADER_SIZE = 6; // 0x46 0x08 (3b) + SN (4b)
    const size_t PDU_ENABLE_EVENTS_MIN_REC_SIZE = 5;
    const size_t PDU_EVENT_MAX_SIZE = 6;
    const size_t PDU_EVENTS_RESPONSE_NO_EVENTS_SIZE = 2; // 0x46 0x12
    const size_t PDU_EVENTS_RESPONSE_MIN_SIZE = PDU_EVENTS_RESPONSE_NO_EVENTS_SIZE;
    const size_t PDU_EVENTS_RESPONSE_HEADER_SIZE = 5; // 0x46 0x11 Flag Nevents DataSize
    const size_t PDU_EVENTS_REQUEST_MAX_BYTES =
        RTU_MAX_PACKET_SIZE - PDU_EVENTS_RESPONSE_HEADER_SIZE - CRC_SIZE - RTU_HEADER_SIZE;

    const size_t PDU_SUB_COMMAND_POS = 1;

    const size_t PDU_SCAN_RESPONSE_NO_MORE_DEVICES_SIZE = 2;
    const size_t PDU_SCAN_RESPONSE_DEVICE_FOUND_SIZE = 7;
    const size_t PDU_SCAN_RESPONSE_MIN_SIZE = PDU_SCAN_RESPONSE_NO_MORE_DEVICES_SIZE;

    const size_t PDU_SCAN_RESPONSE_SN_POS = 2;
    const size_t PDU_SCAN_RESPONSE_SLAVE_ID_POS = 6;

    const size_t PDU_EVENTS_RESPONSE_CONFIRM_FLAG_POS = 2;
    const size_t PDU_EVENTS_RESPONSE_DATA_SIZE_POS = 4;
    const size_t PDU_EVENTS_RESPONSE_DATA_POS = 5;

    const size_t PDU_ENABLE_EVENTS_RESPONSE_MIN_SIZE = 3;
    const size_t PDU_ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS = 2;
    const size_t PDU_ENABLE_EVENTS_RESPONSE_DATA_POS = 3;

    const size_t PDU_MODBUS_STANDARD_COMMAND_RESPONSE_SN_POS = 2;

    // RTU
    const size_t RTU_ARBITRATION_HEADER_MAX_BYTES = 32;

    const size_t RTU_EVENTS_RESPONSE_NO_EVENTS_SIZE = PDU_EVENTS_RESPONSE_NO_EVENTS_SIZE + CRC_SIZE + RTU_HEADER_SIZE;
    const size_t RTU_SCAN_RESPONSE_NO_MORE_DEVICES_SIZE =
        PDU_SCAN_RESPONSE_NO_MORE_DEVICES_SIZE + CRC_SIZE + RTU_HEADER_SIZE;
    const size_t RTU_SCAN_RESPONSE_DEVICE_FOUND_SIZE = PDU_SCAN_RESPONSE_DEVICE_FOUND_SIZE + CRC_SIZE + RTU_HEADER_SIZE;

    const size_t RTU_COMMAND_POS = 1;
    const size_t RTU_SUB_COMMAND_POS = 2;

    const size_t RTU_EVENTS_RESPONSE_DATA_SIZE_POS = PDU_EVENTS_RESPONSE_DATA_SIZE_POS + RTU_HEADER_SIZE;

    // Event record
    const size_t EVENT_HEADER_SIZE = 4;

    const size_t EVENT_SIZE_POS = 0;
    const size_t EVENT_TYPE_POS = 1;
    const size_t EVENT_ID_POS = 2;
    const size_t EVENT_DATA_POS = 4;

    // max(3.5 symbols, (20 bits + 800us)) + 9 * max(13 bits, 12 bits + 50us)
    std::chrono::milliseconds GetTimeoutForEvents(const TPort& port)
    {
        const auto cmdTime =
            std::max(port.GetSendTimeBytes(Modbus::STANDARD_FRAME_TIMEOUT_BYTES), port.GetSendTimeBits(20) + 800us);
        const auto arbitrationTime = 9 * std::max(port.GetSendTimeBits(13), port.GetSendTimeBits(12) + 50us);
        return std::chrono::ceil<std::chrono::milliseconds>(cmdTime + arbitrationTime);
    }

    // For 0x46 command: max(3.5 symbols, (20 bits + 800us)) + 33 * max(13 bits, 12 bits + 50us)
    // For 0x60 command: (44 bits) + 33 * 20 bits
    std::chrono::microseconds GetTimeoutForScan(const TPort& port, TModbusExtCommand command)
    {
        if (command == TModbusExtCommand::DEPRECATED) {
            return port.GetSendTimeBits(44) + 33 * port.GetSendTimeBits(20);
        }
        const auto cmdTime =
            std::max(port.GetSendTimeBytes(Modbus::STANDARD_FRAME_TIMEOUT_BYTES), port.GetSendTimeBits(20) + 800us);
        const auto arbitrationTime = 33 * std::max(port.GetSendTimeBits(13), port.GetSendTimeBits(12) + 50us);
        return cmdTime + arbitrationTime;
    }

    uint8_t GetMaxReadEventsResponseDataSize(const TPort& port, std::chrono::milliseconds maxTime)
    {
        // Read at least one event regardless of time
        if (maxTime.count() <= 0) {
            return PDU_EVENT_MAX_SIZE;
        }
        auto timePerByte = port.GetSendTimeBytes(1);
        if (timePerByte.count() == 0) {
            return PDU_EVENTS_REQUEST_MAX_BYTES;
        }
        size_t maxBytes = std::chrono::duration_cast<std::chrono::microseconds>(maxTime).count() / timePerByte.count();
        if (maxBytes > RTU_MAX_PACKET_SIZE) {
            maxBytes = RTU_MAX_PACKET_SIZE;
        }
        if (maxBytes <= PDU_EVENTS_RESPONSE_HEADER_SIZE + PDU_EVENT_MAX_SIZE) {
            return PDU_EVENT_MAX_SIZE;
        }
        maxBytes -= PDU_EVENTS_RESPONSE_HEADER_SIZE + CRC_SIZE + RTU_HEADER_SIZE;
        return static_cast<uint8_t>(maxBytes);
    }

    std::vector<uint8_t> MakeReadEventsRequest(const TEventConfirmationState& state,
                                               uint8_t startingSlaveId,
                                               uint8_t maxBytes)
    {
        std::vector<uint8_t> request({TModbusExtCommand::ACTUAL, EVENTS_REQUEST_COMMAND});
        auto it = std::back_inserter(request);
        Append(it, startingSlaveId);
        Append(it, maxBytes);
        Append(it, state.SlaveId);
        Append(it, state.Flag);
        return request;
    }

    std::vector<uint8_t> MakeScanRequestPdu(uint8_t fastModbusCommand, uint8_t scanCommand)
    {
        return {fastModbusCommand, scanCommand};
    }

    void CheckCRC16(const uint8_t* packet, size_t size)
    {
        if (size < CRC_SIZE) {
            throw Modbus::TMalformedResponseError("invalid packet size: " + std::to_string(size));
        }

        auto crc = GetBigEndian<uint16_t>(packet + size - CRC_SIZE, packet + size);
        if (crc != CRC16::CalculateCRC16(packet, size - CRC_SIZE)) {
            throw Modbus::TMalformedResponseError("invalid crc");
        }
    }

    bool IsModbusExtCommand(uint8_t command)
    {
        return command == TModbusExtCommand::ACTUAL || command == TModbusExtCommand::DEPRECATED;
    }

    bool IsModbusExtRTUPacket(const uint8_t* buf, size_t size)
    {
        if ((buf[0] == 0xFF) || (RTU_SUB_COMMAND_POS >= size) || (!IsModbusExtCommand(buf[RTU_COMMAND_POS]))) {
            return false;
        }

        switch (buf[RTU_SUB_COMMAND_POS]) {
            case HAS_EVENTS_RESPONSE_COMMAND: {
                if ((size <= RTU_EVENTS_RESPONSE_DATA_SIZE_POS) ||
                    (size != PDU_EVENTS_RESPONSE_HEADER_SIZE + buf[RTU_EVENTS_RESPONSE_DATA_SIZE_POS] + CRC_SIZE +
                                 RTU_HEADER_SIZE))
                {
                    return false;
                }
                break;
            }
            case NO_EVENTS_RESPONSE_COMMAND: {
                if (size != RTU_EVENTS_RESPONSE_NO_EVENTS_SIZE) {
                    return false;
                }
                break;
            }
            case NO_MORE_DEVICES_RESPONSE_SCAN_COMMAND: {
                if (size != RTU_SCAN_RESPONSE_NO_MORE_DEVICES_SIZE) {
                    return false;
                }
                break;
            }
            case DEVICE_FOUND_RESPONSE_SCAN_COMMAND: {
                if (size != RTU_SCAN_RESPONSE_DEVICE_FOUND_SIZE) {
                    return false;
                }
                break;
            }
            default: {
                // Unexpected sub command
                return false;
            }
        }

        try {
            CheckCRC16(buf, size);
        } catch (const Modbus::TMalformedResponseError& err) {
            return false;
        }
        return true;
    }

    const uint8_t* GetRTUPacketStart(const uint8_t* data, size_t size)
    {
        while (size > RTU_SUB_COMMAND_POS) {
            if (IsModbusExtRTUPacket(data, size)) {
                return data;
            }
            ++data;
            --size;
        }
        return nullptr;
    }

    TPort::TFrameCompletePred ExpectFastModbusRTU()
    {
        return [=](uint8_t* buf, size_t size) { return GetRTUPacketStart(buf, size) != nullptr; };
    }

    void IterateOverEvents(uint8_t slaveId, const uint8_t* data, size_t size, IEventsVisitor& eventVisitor)
    {
        while (size != 0) {
            size_t dataSize = data[EVENT_SIZE_POS];
            size_t eventSize = dataSize + EVENT_HEADER_SIZE;
            if (eventSize > size) {
                throw Modbus::TMalformedResponseError("invalid event data size");
            }
            eventVisitor.Event(slaveId,
                               data[EVENT_TYPE_POS],
                               GetFromBigEndian<uint16_t>(data + EVENT_ID_POS),
                               data + EVENT_DATA_POS,
                               dataSize);
            data += eventSize;
            size -= eventSize;
        }
    }

    bool IsRegisterEvent(uint8_t eventType)
    {
        switch (eventType) {
            case TEventType::COIL:
            case TEventType::DISCRETE:
            case TEventType::HOLDING:
            case TEventType::INPUT:
                return true;
            default:
                return false;
        }
    }

    bool ReadEvents(TPort& port,
                    Modbus::IModbusTraits& traits,
                    std::chrono::milliseconds maxEventsReadTime,
                    uint8_t startingSlaveId,
                    TEventConfirmationState& state,
                    IEventsVisitor& eventVisitor)
    {
        // TODO: Count request and arbitration.
        //       maxEventsReadTime limits not only response, but total request-response time.
        //       So request and arbitration time must be subtracted from time for response
        auto maxBytes = GetMaxReadEventsResponseDataSize(port, maxEventsReadTime);

        auto requestPdu = MakeReadEventsRequest(state, startingSlaveId, maxBytes);
        auto frameTimeout = port.GetSendTimeBytes(Modbus::STANDARD_FRAME_TIMEOUT_BYTES);
        const auto timeout = GetTimeoutForEvents(port);

        port.SleepSinceLastInteraction(frameTimeout);
        Modbus::TReadResult res;
        try {
            res = traits.Transaction(port,
                                     BROADCAST_ADDRESS,
                                     requestPdu,
                                     RTU_MAX_PACKET_SIZE + RTU_ARBITRATION_HEADER_MAX_BYTES,
                                     timeout,
                                     timeout,
                                     false);
        } catch (std::exception& ex) {
            port.SleepSinceLastInteraction(frameTimeout);
            throw;
        }
        port.SleepSinceLastInteraction(frameTimeout);

        if (res.Pdu.size() < PDU_EVENTS_RESPONSE_MIN_SIZE) {
            throw Modbus::TMalformedResponseError("invalid packet size: " + std::to_string(res.Pdu.size()));
        }

        const uint8_t* packet = res.Pdu.data();

        switch (packet[PDU_SUB_COMMAND_POS]) {
            case HAS_EVENTS_RESPONSE_COMMAND: {
                if ((res.Pdu.size() <= PDU_EVENTS_RESPONSE_DATA_SIZE_POS) ||
                    (res.Pdu.size() != PDU_EVENTS_RESPONSE_HEADER_SIZE + packet[PDU_EVENTS_RESPONSE_DATA_SIZE_POS]))
                {
                    throw Modbus::TMalformedResponseError("invalid packet size: " + std::to_string(res.Pdu.size()));
                }

                state.SlaveId = res.SlaveId;
                state.Flag = packet[PDU_EVENTS_RESPONSE_CONFIRM_FLAG_POS];
                IterateOverEvents(res.SlaveId,
                                  packet + PDU_EVENTS_RESPONSE_DATA_POS,
                                  packet[PDU_EVENTS_RESPONSE_DATA_SIZE_POS],
                                  eventVisitor);
                return true;
            }
            case NO_EVENTS_RESPONSE_COMMAND: {
                return false;
            }
            default: {
                throw Modbus::TMalformedResponseError("invalid sub command");
            }
        }
    }

    bool HasSpaceForEnableEventRecord(const std::vector<uint8_t>& requestPdu)
    {
        return requestPdu.size() + requestPdu[PDU_ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS] +
                   PDU_ENABLE_EVENTS_MIN_REC_SIZE <
               RTU_MAX_PACKET_SIZE - CRC_SIZE - RTU_HEADER_SIZE;
    }

    //=====================================================
    //                TBitIterator
    //=====================================================

    class TBitIterator
    {
        uint8_t* Data;
        const uint8_t* End;
        size_t BitIndex;

    public:
        TBitIterator(uint8_t* data, size_t size): Data(data), End(data + size), BitIndex(0)
        {}

        void NextBit()
        {
            ++BitIndex;
            if (BitIndex == 8) {
                NextByte();
            }
        }

        void NextByte()
        {
            ++Data;
            BitIndex = 0;
            if (Data > End) {
                throw Modbus::TMalformedResponseError("not enough data in enable events response");
            }
        }

        bool GetBit() const
        {
            return (*Data >> BitIndex) & 0x01;
        }
    };

    //=====================================================
    //                TEventsEnabler
    //=====================================================

    void TEventsEnabler::EnableEvents(const std::vector<uint8_t>& requestPdu)
    {
        // Use response timeout from MR6C template
        auto res = Traits.Transaction(Port,
                                      SlaveId,
                                      requestPdu,
                                      RTU_MAX_PACKET_SIZE + RTU_ARBITRATION_HEADER_MAX_BYTES,
                                      8ms,
                                      FrameTimeout);

        // Old firmwares can send any command with exception bit
        if (res.Pdu.size() == 2 && Modbus::IsException(res.Pdu[0])) {
            throw Modbus::TModbusExceptionError(res.Pdu[1]);
        }

        if (res.Pdu.size() < PDU_ENABLE_EVENTS_RESPONSE_MIN_SIZE) {
            throw Modbus::TMalformedResponseError("invalid packet size: " + std::to_string(res.Pdu.size()));
        }

        if (res.Pdu[PDU_SUB_COMMAND_POS] != ENABLE_EVENTS_COMMAND) {
            throw Modbus::TMalformedResponseError("invalid sub command");
        }

        TBitIterator dataIt(res.Pdu.data() + PDU_ENABLE_EVENTS_RESPONSE_DATA_POS - 1,
                            res.Pdu[PDU_ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS]);

        TEventType lastType = TEventType::REBOOT;
        uint16_t lastRegAddr = 0;
        uint16_t lastDataAddr = 0;
        bool firstRegister = true;

        for (auto regIt = SettingsStart; regIt != SettingsEnd; ++regIt) {
            if (firstRegister || (lastType != regIt->Type) || (lastRegAddr + MaxRegDistance < regIt->Addr)) {
                dataIt.NextByte();
                lastDataAddr = regIt->Addr;
                lastType = regIt->Type;
                Visitor(lastType, lastDataAddr, dataIt.GetBit());
                firstRegister = false;
            } else {
                do {
                    dataIt.NextBit();
                    ++lastDataAddr;
                    Visitor(lastType, lastDataAddr, dataIt.GetBit());
                } while (lastDataAddr != regIt->Addr);
            }
            lastRegAddr = regIt->Addr;
        }
    }

    TEventsEnabler::TEventsEnabler(uint8_t slaveId,
                                   TPort& port,
                                   Modbus::IModbusTraits& traits,
                                   TEventsEnabler::TVisitorFn visitor,
                                   TEventsEnablerFlags flags)
        : SlaveId(slaveId),
          Port(port),
          Traits(traits),
          MaxRegDistance(1),
          Visitor(visitor)
    {
        if (flags == TEventsEnablerFlags::DISABLE_EVENTS_IN_HOLES) {
            MaxRegDistance = PDU_ENABLE_EVENTS_MIN_REC_SIZE;
        }
        FrameTimeout =
            std::chrono::ceil<std::chrono::milliseconds>(port.GetSendTimeBytes(Modbus::STANDARD_FRAME_TIMEOUT_BYTES));
    }

    void TEventsEnabler::AddRegister(uint16_t addr, TEventType type, TEventPriority priority)
    {
        Settings.emplace_back(TRegisterToEnable{type, addr, priority});
    }

    void TEventsEnabler::SendSingleRequest()
    {
        std::vector<uint8_t> requestPdu({TModbusExtCommand::ACTUAL, ENABLE_EVENTS_COMMAND, 0});
        auto requestBack = std::back_inserter(requestPdu);
        TEventType lastType = TEventType::REBOOT;
        uint16_t lastAddr = 0;
        auto regIt = SettingsEnd;
        size_t regCountPos;
        bool firstRegister = true;
        for (; HasSpaceForEnableEventRecord(requestPdu) && regIt != Settings.cend(); ++regIt) {
            if (firstRegister || (lastType != regIt->Type) || (lastAddr + MaxRegDistance < regIt->Addr)) {
                Append(requestBack, static_cast<uint8_t>(regIt->Type));
                AppendBigEndian(requestBack, regIt->Addr);
                Append(requestBack, static_cast<uint8_t>(1));
                regCountPos = requestPdu.size() - 1;
                Append(requestBack, static_cast<uint8_t>(regIt->Priority));
                requestPdu[PDU_ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS] += PDU_ENABLE_EVENTS_MIN_REC_SIZE;
                firstRegister = false;
            } else {
                const auto nRegs = static_cast<size_t>(regIt->Addr - lastAddr);
                requestPdu[regCountPos] += nRegs;
                for (size_t i = 0; i + 1 < nRegs; ++i) {
                    Append(requestBack, static_cast<uint8_t>(TEventPriority::DISABLE));
                }
                Append(requestBack, static_cast<uint8_t>(regIt->Priority));
                requestPdu[PDU_ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS] += nRegs;
            }
            lastType = regIt->Type;
            lastAddr = regIt->Addr;
        }

        SettingsStart = SettingsEnd;
        SettingsEnd = regIt;

        EnableEvents(requestPdu);
    }

    void TEventsEnabler::SendRequests()
    {
        if (Settings.empty()) {
            return;
        }
        std::sort(Settings.begin(), Settings.end(), [](const auto& a, const auto& b) {
            if (a.Type == b.Type) {
                return a.Addr < b.Addr;
            }
            return a.Type < b.Type;
        });
        auto last = std::unique(Settings.begin(), Settings.end(), [](const auto& a, const auto& b) {
            return a.Type == b.Type && a.Addr == b.Addr;
        });
        Settings.erase(last, Settings.end());
        for (SettingsEnd = SettingsStart = Settings.cbegin(); SettingsEnd != Settings.cend();) {
            SendSingleRequest();
        }
    }

    bool TEventsEnabler::HasEventsToSetup() const
    {
        return !Settings.empty();
    }

    void TEventConfirmationState::Reset()
    {
        Flag = 0;
        SlaveId = 0;
    }

    //=========================================================
    //                    TModbusTraits
    //=========================================================

    TModbusTraits::TModbusTraits(std::unique_ptr<Modbus::IModbusTraits> baseTraits)
        : Sn(0),
          ModbusExtCommand(TModbusExtCommand::ACTUAL),
          BaseTraits(std::move(baseTraits))
    {}

    size_t TModbusTraits::GetIntermediatePduSize(size_t pduSize) const
    {
        return PDU_MODBUS_STANDARD_COMMAND_HEADER_SIZE + pduSize;
    }

    void TModbusTraits::FinalizeIntermediatePdu(std::vector<uint8_t>& request)
    {
        request[0] = ModbusExtCommand;
        request[1] = MODBUS_STANDARD_REQUEST_COMMAND;
        request[2] = static_cast<uint8_t>(Sn >> 24);
        request[3] = static_cast<uint8_t>(Sn >> 16);
        request[4] = static_cast<uint8_t>(Sn >> 8);
        request[5] = static_cast<uint8_t>(Sn);
    }

    void TModbusTraits::ValidateIntermediateResponsePdu(const std::vector<uint8_t>& response) const
    {
        if (response.size() < PDU_MODBUS_STANDARD_COMMAND_HEADER_SIZE) {
            throw Modbus::TMalformedResponseError("invalid data size");
        }

        if (response[0] != ModbusExtCommand) {
            throw Modbus::TUnexpectedResponseError("invalid response command");
        }

        if (response[1] != MODBUS_STANDARD_RESPONSE_COMMAND) {
            throw Modbus::TUnexpectedResponseError("invalid response subcommand: " + std::to_string(response[1]));
        }

        auto responseSn = GetBigEndian<uint32_t>(response.cbegin() + PDU_MODBUS_STANDARD_COMMAND_RESPONSE_SN_POS,
                                                 response.cbegin() + PDU_MODBUS_STANDARD_COMMAND_HEADER_SIZE);
        if (responseSn != Sn) {
            throw Modbus::TUnexpectedResponseError("SN mismatch: got " + std::to_string(responseSn) + ", wait " +
                                                   std::to_string(Sn));
        }
    }

    Modbus::TReadResult TModbusTraits::Transaction(TPort& port,
                                                   uint8_t slaveId,
                                                   const std::vector<uint8_t>& requestPdu,
                                                   size_t expectedResponsePduSize,
                                                   const std::chrono::milliseconds& responseTimeout,
                                                   const std::chrono::milliseconds& frameTimeout,
                                                   bool matchSlaveId)
    {
        std::vector<uint8_t> request(GetIntermediatePduSize(requestPdu.size()));
        std::copy(requestPdu.begin(), requestPdu.end(), request.begin() + PDU_MODBUS_STANDARD_COMMAND_HEADER_SIZE);
        FinalizeIntermediatePdu(request);

        auto intermediateResponse =
            BaseTraits->Transaction(port,
                                    slaveId,
                                    request,
                                    expectedResponsePduSize + PDU_MODBUS_STANDARD_COMMAND_HEADER_SIZE,
                                    responseTimeout,
                                    frameTimeout,
                                    matchSlaveId);

        ValidateIntermediateResponsePdu(intermediateResponse.Pdu);

        Modbus::TReadResult res;
        res.ResponseTime = intermediateResponse.ResponseTime;
        res.Pdu.assign(intermediateResponse.Pdu.begin() + PDU_MODBUS_STANDARD_COMMAND_HEADER_SIZE,
                       intermediateResponse.Pdu.end());
        return res;
    }

    void TModbusTraits::SetSn(uint32_t sn)
    {
        Sn = sn;
    }

    void TModbusTraits::SetModbusExtCommand(TModbusExtCommand command)
    {
        ModbusExtCommand = command;
    }

    //=========================================================

    std::optional<TScannedDevice> SendScanRequest(TPort& port,
                                                  Modbus::IModbusTraits& traits,
                                                  TModbusExtCommand modbusExtCommand,
                                                  uint8_t scanCommand)
    {
        auto req = MakeScanRequestPdu(modbusExtCommand, scanCommand);
        const auto standardFrameTimeout = port.GetSendTimeBytes(Modbus::STANDARD_FRAME_TIMEOUT_BYTES);
        const auto scanFrameTimeout = GetTimeoutForScan(port, modbusExtCommand);
        const auto responseTimeout = scanFrameTimeout + port.GetSendTimeBytes(1);

        port.SleepSinceLastInteraction(standardFrameTimeout);
        auto res = traits.Transaction(port,
                                      BROADCAST_ADDRESS,
                                      req,
                                      RTU_MAX_PACKET_SIZE + RTU_ARBITRATION_HEADER_MAX_BYTES,
                                      std::chrono::ceil<std::chrono::milliseconds>(responseTimeout),
                                      std::chrono::ceil<std::chrono::milliseconds>(scanFrameTimeout));

        if (res.Pdu.size() < PDU_SCAN_RESPONSE_MIN_SIZE) {
            throw Modbus::TMalformedResponseError("invalid packet");
        }

        const uint8_t* packet = res.Pdu.data();
        switch (packet[PDU_SUB_COMMAND_POS]) {
            case DEVICE_FOUND_RESPONSE_SCAN_COMMAND: {
                if (res.Pdu.size() < PDU_SCAN_RESPONSE_DEVICE_FOUND_SIZE) {
                    throw Modbus::TMalformedResponseError("invalid device found response packet size");
                }
                return std::optional<TScannedDevice>{
                    TScannedDevice{packet[PDU_SCAN_RESPONSE_SLAVE_ID_POS],
                                   BinUtils::GetFromBigEndian<uint32_t>(packet + PDU_SCAN_RESPONSE_SN_POS)}};
            }
            case NO_MORE_DEVICES_RESPONSE_SCAN_COMMAND: {
                return std::nullopt;
            }
            default: {
                throw Modbus::TMalformedResponseError("invalid sub command");
            }
        }
    }

    std::optional<TScannedDevice> ScanStart(TPort& port,
                                            Modbus::IModbusTraits& traits,
                                            TModbusExtCommand modbusExtCommand)
    {
        try {
            return SendScanRequest(port, traits, modbusExtCommand, START_SCAN_COMMAND);
        } catch (const TResponseTimeoutException& ex) {
            // It is ok. No Fast Modbus capable devices on port.
            LOG(Debug) << "No Fast Modbus capable devices on port " << port.GetDescription();
            return std::nullopt;
        }
    }

    std::optional<TScannedDevice> ScanNext(TPort& port,
                                           Modbus::IModbusTraits& traits,
                                           TModbusExtCommand modbusExtCommand)
    {
        return SendScanRequest(port, traits, modbusExtCommand, CONTINUE_SCAN_COMMAND);
    }

    void Scan(TPort& port,
              Modbus::IModbusTraits& traits,
              TModbusExtCommand modbusExtCommand,
              std::vector<TScannedDevice>& scannedDevices)
    {
        auto res = ScanStart(port, traits, modbusExtCommand);
        if (!res) {
            return;
        }
        scannedDevices.push_back(*res);
        while (true) {
            res = ScanNext(port, traits, modbusExtCommand);
            if (!res) {
                return;
            }
            scannedDevices.push_back(*res);
        }
    }

    //=========================================================
    //             TModbusRTUWithArbitrationTraits
    //=========================================================

    TModbusRTUWithArbitrationTraits::TModbusRTUWithArbitrationTraits()
    {}

    TPort::TFrameCompletePred TModbusRTUWithArbitrationTraits::ExpectFastModbusRTU() const
    {
        return [=](uint8_t* buf, size_t size) { return GetRTUPacketStart(buf, size) != nullptr; };
    }

    void TModbusRTUWithArbitrationTraits::FinalizeRequest(std::vector<uint8_t>& request, uint8_t slaveId)
    {
        request[0] = slaveId;
        WriteAs2Bytes(&request[request.size() - 2], CRC16::CalculateCRC16(request.data(), request.size() - 2));
    }

    Modbus::TReadResult TModbusRTUWithArbitrationTraits::Transaction(TPort& port,
                                                                     uint8_t slaveId,
                                                                     const std::vector<uint8_t>& requestPdu,
                                                                     size_t expectedResponsePduSize,
                                                                     const std::chrono::milliseconds& responseTimeout,
                                                                     const std::chrono::milliseconds& frameTimeout,
                                                                     bool matchSlaveId)
    {
        std::vector<uint8_t> request(requestPdu.size() + 3);
        std::copy(requestPdu.begin(), requestPdu.end(), request.begin() + 1);
        FinalizeRequest(request, slaveId);

        port.WriteBytes(request);

        std::array<uint8_t, RTU_MAX_PACKET_SIZE + RTU_ARBITRATION_HEADER_MAX_BYTES> response;
        auto res =
            port.ReadFrame(response.data(), response.size(), responseTimeout, frameTimeout, ExpectFastModbusRTU());

        const uint8_t* packet = GetRTUPacketStart(response.data(), res.Count);
        if (packet == nullptr) {
            throw Modbus::TMalformedResponseError("invalid packet");
        }
        if (matchSlaveId && (packet[0] != slaveId)) {
            throw Modbus::TUnexpectedResponseError("slave id mismatch");
        }
        Modbus::TReadResult result;
        result.ResponseTime = res.ResponseTime;
        result.Pdu.assign(packet + RTU_HEADER_SIZE, response.cbegin() + (res.Count - CRC_SIZE));
        result.SlaveId = packet[0];
        return result;
    }

}
