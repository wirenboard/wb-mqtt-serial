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
    const uint8_t EVENTS_RESPONSE_COMMAND = 0x11;
    const uint8_t NO_EVENTS_RESPONSE_COMMAND = 0x12;
    const uint8_t ENABLE_EVENTS_COMMAND = 0x18;

    const size_t NO_EVENTS_RESPONSE_SIZE = 5;
    const size_t EVENTS_RESPONSE_HEADER_SIZE = 6;
    const size_t CRC_SIZE = 2;
    const size_t MAX_PACKET_SIZE = 256;
    const size_t EVENT_HEADER_SIZE = 4;
    const size_t MIN_ENABLE_EVENTS_RESPONSE_SIZE = 6;
    const size_t MIN_ENABLE_EVENTS_REC_SIZE = 5;
    // const size_t EXCEPTION_SIZE = 5;
    const size_t MODBUS_STANDARD_COMMAND_HEADER_SIZE = 7; // 0xFD 0x46 0x08 (3b) + SN (4b)
    const size_t NO_MORE_DEVICES_RESPONSE_SIZE = 5;
    const size_t DEVICE_FOUND_RESPONSE_SIZE = 10;

    const size_t EVENTS_REQUEST_MAX_BYTES = MAX_PACKET_SIZE - EVENTS_RESPONSE_HEADER_SIZE - CRC_SIZE;
    const size_t ARBITRATION_HEADER_MAX_BYTES = 32;

    const size_t SLAVE_ID_POS = 0;
    const size_t COMMAND_POS = 1;
    const size_t SUB_COMMAND_POS = 2;
    const size_t EXCEPTION_CODE_POS = 2;

    // const size_t EVENTS_RESPONSE_SLAVE_ID_POS = 0;
    const size_t EVENTS_RESPONSE_CONFIRM_FLAG_POS = 3;
    const size_t EVENTS_RESPONSE_DATA_SIZE_POS = 5;
    const size_t EVENTS_RESPONSE_DATA_POS = 6;

    const size_t EVENT_SIZE_POS = 0;
    const size_t EVENT_TYPE_POS = 1;
    const size_t EVENT_ID_POS = 2;
    const size_t EVENT_DATA_POS = 4;
    const size_t MAX_EVENT_SIZE = 6;

    const size_t SCAN_SN_POS = 3;
    const size_t SCAN_SLAVE_ID_POS = 7;

    const size_t ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS = 3;
    const size_t ENABLE_EVENTS_RESPONSE_DATA_POS = 4;

    const size_t MODBUS_STANDARD_COMMAND_RESPONSE_SN_POS = 3;
    const size_t MODBUS_STANDARD_COMMAND_PDU_POS = 7;

    // const size_t ENABLE_EVENTS_REC_TYPE_POS = 0;
    // const size_t ENABLE_EVENTS_REC_ADDR_POS = 1;
    // const size_t ENABLE_EVENTS_REC_STATE_POS = 3;

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

    uint8_t GetMaxReadEventsResponseSize(const TPort& port, std::chrono::milliseconds maxTime)
    {
        if (maxTime.count() <= 0) {
            return MAX_EVENT_SIZE;
        }
        auto timePerByte = port.GetSendTimeBytes(1);
        if (timePerByte.count() == 0) {
            return EVENTS_REQUEST_MAX_BYTES;
        }
        size_t maxBytes = std::chrono::duration_cast<std::chrono::microseconds>(maxTime).count() / timePerByte.count();
        if (maxBytes > MAX_PACKET_SIZE) {
            maxBytes = MAX_PACKET_SIZE;
        }
        if (maxBytes <= EVENTS_RESPONSE_HEADER_SIZE + MAX_EVENT_SIZE + CRC_SIZE) {
            return MAX_EVENT_SIZE;
        }
        maxBytes -= EVENTS_RESPONSE_HEADER_SIZE + CRC_SIZE;
        if (maxBytes > std::numeric_limits<uint8_t>::max()) {
            return std::numeric_limits<uint8_t>::max();
        }
        return static_cast<uint8_t>(maxBytes);
    }

    std::vector<uint8_t> MakeReadEventsRequest(const TEventConfirmationState& state,
                                               uint8_t startingSlaveId = 0,
                                               uint8_t maxBytes = EVENTS_REQUEST_MAX_BYTES)
    {
        std::vector<uint8_t> request({BROADCAST_ADDRESS, TModbusExtCommand::ACTUAL, EVENTS_REQUEST_COMMAND});
        auto it = std::back_inserter(request);
        Append(it, startingSlaveId);
        Append(it, maxBytes);
        Append(it, state.SlaveId);
        Append(it, state.Flag);
        AppendBigEndian(it, CRC16::CalculateCRC16(request.data(), request.size()));
        return request;
    }

    std::vector<uint8_t> MakeScanRequest(uint8_t fastModbusCommand, uint8_t scanCommand)
    {
        std::vector<uint8_t> request({BROADCAST_ADDRESS, fastModbusCommand, scanCommand});
        AppendBigEndian(std::back_inserter(request), CRC16::CalculateCRC16(request.data(), request.size()));
        return request;
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

    bool IsModbusExtPacket(const uint8_t* buf, size_t size)
    {
        if ((buf[0] == 0xFF) || (SUB_COMMAND_POS >= size) || (!IsModbusExtCommand(buf[COMMAND_POS]))) {
            return false;
        }

        switch (buf[SUB_COMMAND_POS]) {
            case EVENTS_RESPONSE_COMMAND: {
                if ((size <= EVENTS_RESPONSE_DATA_SIZE_POS) ||
                    (size != EVENTS_RESPONSE_HEADER_SIZE + buf[EVENTS_RESPONSE_DATA_SIZE_POS] + CRC_SIZE))
                {
                    return false;
                }
                break;
            }
            case NO_EVENTS_RESPONSE_COMMAND: {
                if (size != NO_EVENTS_RESPONSE_SIZE) {
                    return false;
                }
                break;
            }
            case NO_MORE_DEVICES_RESPONSE_SCAN_COMMAND: {
                if (size != NO_MORE_DEVICES_RESPONSE_SIZE) {
                    return false;
                }
                break;
            }
            case DEVICE_FOUND_RESPONSE_SCAN_COMMAND: {
                if (size != DEVICE_FOUND_RESPONSE_SIZE) {
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

    const uint8_t* GetPacketStart(const uint8_t* data, size_t size)
    {
        while (size > SUB_COMMAND_POS) {
            if (IsModbusExtPacket(data, size)) {
                return data;
            }
            ++data;
            --size;
        }
        return nullptr;
    }

    TPort::TFrameCompletePred ExpectFastModbus()
    {
        return [=](uint8_t* buf, size_t size) { return GetPacketStart(buf, size) != nullptr; };
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
                    std::chrono::milliseconds maxEventsReadTime,
                    uint8_t startingSlaveId,
                    TEventConfirmationState& state,
                    IEventsVisitor& eventVisitor)
    {
        // TODO: Count request and arbitration.
        //       maxEventsReadTime limits not only response, but total request-response time.
        //       So request and arbitration time must be subtracted from time for response
        auto maxBytes = GetMaxReadEventsResponseSize(port, maxEventsReadTime);

        auto req = MakeReadEventsRequest(state, startingSlaveId, maxBytes);
        auto frameTimeout = port.GetSendTimeBytes(Modbus::STANDARD_FRAME_TIMEOUT_BYTES);
        port.SleepSinceLastInteraction(frameTimeout);
        port.WriteBytes(req);

        const auto timeout = GetTimeoutForEvents(port);
        std::array<uint8_t, MAX_PACKET_SIZE + ARBITRATION_HEADER_MAX_BYTES> res;
        auto rc = port.ReadFrame(res.data(), res.size(), timeout, timeout, ExpectFastModbus()).Count;
        port.SleepSinceLastInteraction(frameTimeout);

        const uint8_t* packet = GetPacketStart(res.data(), rc);
        if (packet == nullptr) {
            throw Modbus::TMalformedResponseError("invalid packet");
        }

        switch (packet[SUB_COMMAND_POS]) {
            case EVENTS_RESPONSE_COMMAND: {
                state.SlaveId = packet[SLAVE_ID_POS];
                state.Flag = packet[EVENTS_RESPONSE_CONFIRM_FLAG_POS];
                IterateOverEvents(packet[SLAVE_ID_POS],
                                  packet + EVENTS_RESPONSE_DATA_POS,
                                  packet[EVENTS_RESPONSE_DATA_SIZE_POS],
                                  eventVisitor);
                return true;
            }
            case NO_EVENTS_RESPONSE_COMMAND: {
                if (packet[0] != BROADCAST_ADDRESS) {
                    throw Modbus::TMalformedResponseError("invalid address");
                }
                return false;
            }
            default: {
                throw Modbus::TMalformedResponseError("invalid sub command");
            }
        }
    }

    bool HasSpaceForEnableEventRecord(const std::vector<uint8_t>& request)
    {
        return request.size() + request[ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS] + CRC_SIZE + MIN_ENABLE_EVENTS_REC_SIZE <
               request.capacity();
    }

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

    void TEventsEnabler::EnableEvents()
    {
        Port.WriteBytes(Request);

        // Use response timeout from MR6C template
        auto rc = Port.ReadFrame(Response.data(), Request.size(), 8ms, FrameTimeout).Count;

        CheckCRC16(Response.data(), rc);

        // Old firmwares can send any command with exception bit
        if (Modbus::IsException(Response[COMMAND_POS])) {
            throw Modbus::TModbusExceptionError(Response[EXCEPTION_CODE_POS]);
        }

        if (rc < MIN_ENABLE_EVENTS_RESPONSE_SIZE) {
            throw Modbus::TMalformedResponseError("invalid packet size: " + std::to_string(rc));
        }

        if (Response[SLAVE_ID_POS] != SlaveId) {
            throw Modbus::TMalformedResponseError("invalid slave id");
        }

        if (Response[SUB_COMMAND_POS] != ENABLE_EVENTS_COMMAND) {
            throw Modbus::TMalformedResponseError("invalid sub command");
        }

        TBitIterator dataIt(Response.data() + ENABLE_EVENTS_RESPONSE_DATA_POS - 1,
                            Response[ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS]);

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

    void TEventsEnabler::ClearRequest()
    {
        Request.erase(Request.begin() + ENABLE_EVENTS_RESPONSE_DATA_POS, Request.end());
        Request[ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS] = 0;
    }

    TEventsEnabler::TEventsEnabler(uint8_t slaveId,
                                   TPort& port,
                                   TEventsEnabler::TVisitorFn visitor,
                                   TEventsEnablerFlags flags)
        : SlaveId(slaveId),
          Port(port),
          MaxRegDistance(1),
          Visitor(visitor)
    {
        if (flags == TEventsEnablerFlags::DISABLE_EVENTS_IN_HOLES) {
            MaxRegDistance = MIN_ENABLE_EVENTS_REC_SIZE;
        }
        Request.reserve(MAX_PACKET_SIZE);
        Append(std::back_inserter(Request), {SlaveId, TModbusExtCommand::ACTUAL, ENABLE_EVENTS_COMMAND, 0});
        Response.reserve(MAX_PACKET_SIZE);
        FrameTimeout =
            std::chrono::ceil<std::chrono::milliseconds>(port.GetSendTimeBytes(Modbus::STANDARD_FRAME_TIMEOUT_BYTES));
    }

    void TEventsEnabler::AddRegister(uint16_t addr, TEventType type, TEventPriority priority)
    {
        Settings.emplace_back(TRegisterToEnable{type, addr, priority});
    }

    void TEventsEnabler::SendSingleRequest()
    {
        ClearRequest();
        auto requestBack = std::back_inserter(Request);
        TEventType lastType = TEventType::REBOOT;
        uint16_t lastAddr = 0;
        auto regIt = SettingsEnd;
        size_t regCountPos;
        bool firstRegister = true;
        for (; HasSpaceForEnableEventRecord(Request) && regIt != Settings.cend(); ++regIt) {
            if (firstRegister || (lastType != regIt->Type) || (lastAddr + MaxRegDistance < regIt->Addr)) {
                Append(requestBack, static_cast<uint8_t>(regIt->Type));
                AppendBigEndian(requestBack, regIt->Addr);
                Append(requestBack, static_cast<uint8_t>(1));
                regCountPos = Request.size() - 1;
                Append(requestBack, static_cast<uint8_t>(regIt->Priority));
                Request[ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS] += MIN_ENABLE_EVENTS_REC_SIZE;
                firstRegister = false;
            } else {
                const auto nRegs = static_cast<size_t>(regIt->Addr - lastAddr);
                Request[regCountPos] += nRegs;
                for (size_t i = 0; i + 1 < nRegs; ++i) {
                    Append(requestBack, static_cast<uint8_t>(TEventPriority::DISABLE));
                }
                Append(requestBack, static_cast<uint8_t>(regIt->Priority));
                Request[ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS] += nRegs;
            }
            lastType = regIt->Type;
            lastAddr = regIt->Addr;
        }

        SettingsStart = SettingsEnd;
        SettingsEnd = regIt;

        AppendBigEndian(std::back_inserter(Request), CRC16::CalculateCRC16(Request.data(), Request.size()));
        EnableEvents();
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

    // TModbusTraits

    TModbusTraits::TModbusTraits(): Sn(0), ModbusExtCommand(TModbusExtCommand::ACTUAL)
    {}

    TPort::TFrameCompletePred TModbusTraits::ExpectNBytes(size_t n) const
    {
        return [=](uint8_t* buf, size_t size) {
            if (size < MODBUS_STANDARD_COMMAND_HEADER_SIZE + 1)
                return false;
            if (Modbus::IsException(buf[MODBUS_STANDARD_COMMAND_PDU_POS])) // GetPDU
                return size >= MODBUS_STANDARD_COMMAND_HEADER_SIZE + Modbus::EXCEPTION_RESPONSE_PDU_SIZE + CRC_SIZE;
            return size >= n;
        };
    }

    size_t TModbusTraits::GetPacketSize(size_t pduSize) const
    {
        return MODBUS_STANDARD_COMMAND_HEADER_SIZE + CRC_SIZE + pduSize;
    }

    void TModbusTraits::FinalizeRequest(std::vector<uint8_t>& request)
    {
        request[0] = BROADCAST_ADDRESS;
        request[1] = ModbusExtCommand;
        request[2] = MODBUS_STANDARD_REQUEST_COMMAND;
        request[3] = static_cast<uint8_t>(Sn >> 24);
        request[4] = static_cast<uint8_t>(Sn >> 16);
        request[5] = static_cast<uint8_t>(Sn >> 8);
        request[6] = static_cast<uint8_t>(Sn);
        auto crc = CRC16::CalculateCRC16(request.data(), request.size() - CRC_SIZE);
        request[request.size() - 2] = static_cast<uint8_t>(crc >> 8);
        request[request.size() - 1] = static_cast<uint8_t>(crc);
    }

    TReadFrameResult TModbusTraits::ReadFrame(TPort& port,
                                              const milliseconds& responseTimeout,
                                              const milliseconds& frameTimeout,
                                              std::vector<uint8_t>& res) const
    {
        auto rc = port.ReadFrame(res.data(),
                                 res.size(),
                                 responseTimeout + frameTimeout,
                                 frameTimeout,
                                 ExpectNBytes(res.size()));

        if (rc.Count < MODBUS_STANDARD_COMMAND_HEADER_SIZE + CRC_SIZE) {
            throw Modbus::TMalformedResponseError("invalid data size");
        }

        uint16_t crc = (res[rc.Count - 2] << 8) + res[rc.Count - 1];
        if (crc != CRC16::CalculateCRC16(res.data(), rc.Count - CRC_SIZE)) {
            throw Modbus::TMalformedResponseError("invalid crc");
        }

        if (res[0] != BROADCAST_ADDRESS) {
            throw Modbus::TUnexpectedResponseError("invalid response address");
        }

        if (res[1] != ModbusExtCommand) {
            throw Modbus::TUnexpectedResponseError("invalid response command");
        }

        if (res[2] != MODBUS_STANDARD_RESPONSE_COMMAND) {
            throw Modbus::TUnexpectedResponseError("invalid response subcommand");
        }

        auto responseSn = GetBigEndian<uint32_t>(res.cbegin() + MODBUS_STANDARD_COMMAND_RESPONSE_SN_POS,
                                                 res.cbegin() + MODBUS_STANDARD_COMMAND_HEADER_SIZE);
        if (responseSn != Sn) {
            throw Modbus::TUnexpectedResponseError("SN mismatch: got " + std::to_string(responseSn) + ", wait " +
                                                   std::to_string(Sn));
        }
        return rc;
    }

    Modbus::TReadResult TModbusTraits::Transaction(TPort& port,
                                                   uint8_t slaveId,
                                                   const std::vector<uint8_t>& requestPdu,
                                                   size_t expectedResponsePduSize,
                                                   const std::chrono::milliseconds& responseTimeout,
                                                   const std::chrono::milliseconds& frameTimeout)
    {
        std::vector<uint8_t> request(GetPacketSize(requestPdu.size()));
        std::copy(requestPdu.begin(), requestPdu.end(), request.begin() + MODBUS_STANDARD_COMMAND_PDU_POS);
        FinalizeRequest(request);

        port.WriteBytes(request.data(), request.size());

        std::vector<uint8_t> response(GetPacketSize(expectedResponsePduSize));

        auto readRes = ReadFrame(port, responseTimeout, frameTimeout, response);

        Modbus::TReadResult res;
        res.ResponseTime = readRes.ResponseTime;
        res.Pdu.assign(response.begin() + MODBUS_STANDARD_COMMAND_HEADER_SIZE,
                       response.begin() + (readRes.Count - CRC_SIZE));
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

    void Scan(TPort& port, TModbusExtCommand modbusExtCommand, std::vector<TScannedDevice>& scannedDevices)
    {
        auto req = MakeScanRequest(modbusExtCommand, START_SCAN_COMMAND);
        const auto standardFrameTimeout = port.GetSendTimeBytes(Modbus::STANDARD_FRAME_TIMEOUT_BYTES);
        port.SleepSinceLastInteraction(standardFrameTimeout);
        port.WriteBytes(req);

        const auto scanFrameTimeout = GetTimeoutForScan(port, modbusExtCommand);
        const auto responseTimeout = scanFrameTimeout + port.GetSendTimeBytes(1);
        std::array<uint8_t, MAX_PACKET_SIZE + ARBITRATION_HEADER_MAX_BYTES> response;
        size_t rc;
        try {
            rc = port.ReadFrame(response.data(), response.size(), responseTimeout, scanFrameTimeout, ExpectFastModbus())
                     .Count;
        } catch (const TResponseTimeoutException& ex) {
            // It is ok. No Fast Modbus capable devices on port.
            LOG(Debug) << "No Fast Modbus capable devices on port " << port.GetDescription();
            return;
        }

        req = MakeScanRequest(modbusExtCommand, CONTINUE_SCAN_COMMAND);
        while (true) {
            const uint8_t* packet = GetPacketStart(response.data(), rc);
            if (packet == nullptr) {
                throw Modbus::TMalformedResponseError("invalid packet");
            }

            switch (packet[SUB_COMMAND_POS]) {
                case DEVICE_FOUND_RESPONSE_SCAN_COMMAND: {
                    scannedDevices.emplace_back(
                        TScannedDevice{packet[SCAN_SLAVE_ID_POS], GetFromBigEndian<uint32_t>(packet + SCAN_SN_POS)});
                    port.SleepSinceLastInteraction(standardFrameTimeout);
                    port.WriteBytes(req);
                    rc = port.ReadFrame(response.data(),
                                        response.size(),
                                        responseTimeout,
                                        scanFrameTimeout,
                                        ExpectFastModbus())
                             .Count;
                    break;
                }
                case NO_MORE_DEVICES_RESPONSE_SCAN_COMMAND: {
                    return;
                }
                default: {
                    throw Modbus::TMalformedResponseError("invalid sub command");
                }
            }
        }
    }
}
