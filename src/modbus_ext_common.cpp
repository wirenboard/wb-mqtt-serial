#include "modbus_ext_common.h"

#include "bin_utils.h"
#include "crc16.h"
#include "log.h"
#include "modbus_common.h"
#include <stddef.h>
#include <stdint.h>
#include <vector>

using namespace BinUtils;
using namespace std::chrono_literals;

#define LOG(logger) logger.Log() << "[modbus-ext] "

namespace ModbusExt // modbus extension protocol declarations
{
    const uint8_t BROADCAST_ADDRESS = 0xFD;

    const uint8_t MODBUS_EXT_COMMAND = 0x46;

    // Function codes
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
    const size_t EXCEPTION_SIZE = 5;

    const size_t EVENTS_REQUEST_MAX_BYTES = MAX_PACKET_SIZE - EVENTS_RESPONSE_HEADER_SIZE - CRC_SIZE;
    const size_t ARBITRATION_HEADER_MAX_BYTES = 32;

    const size_t SLAVE_ID_POS = 0;
    const size_t COMMAND_POS = 1;
    const size_t SUB_COMMAND_POS = 2;
    const size_t EXCEPTION_CODE_POS = 2;

    const size_t EVENTS_RESPONSE_SLAVE_ID_POS = 0;
    const size_t EVENTS_RESPONSE_CONFIRM_FLAG_POS = 3;
    const size_t EVENTS_RESPONSE_DATA_SIZE_POS = 5;
    const size_t EVENTS_RESPONSE_DATA_POS = 6;

    const size_t EVENT_SIZE_POS = 0;
    const size_t EVENT_TYPE_POS = 1;
    const size_t EVENT_ID_POS = 2;
    const size_t EVENT_DATA_POS = 4;
    const size_t MAX_EVENT_SIZE = 6;

    const size_t ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS = 3;
    const size_t ENABLE_EVENTS_RESPONSE_DATA_POS = 4;

    const size_t ENABLE_EVENTS_REC_TYPE_POS = 0;
    const size_t ENABLE_EVENTS_REC_ADDR_POS = 1;
    const size_t ENABLE_EVENTS_REC_STATE_POS = 3;

    // max(3.5 symbols, (20 bits + 800us)) + 9 * max(13 bits, 12 bits + 50us)
    std::chrono::milliseconds GetTimeout(const TPort& port)
    {
        const auto cmdTime = std::max(port.GetSendTimeBytes(3.5), port.GetSendTimeBits(20) + 800us);
        const auto arbitrationTime = 9 * std::max(port.GetSendTimeBits(13), port.GetSendTimeBits(12) + 50us);
        return std::chrono::ceil<std::chrono::milliseconds>(cmdTime + arbitrationTime);
    }

    size_t GetPacketStart(const uint8_t* data, size_t size)
    {
        for (size_t i = 0; i < size; ++i) {
            if (data[i] != 0xFF) {
                return i;
            }
        }
        return size;
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
        auto maxBytes = std::chrono::duration_cast<std::chrono::microseconds>(maxTime).count() / timePerByte.count();
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

    Modbus::TRequest MakeReadEventsRequest(const TEventConfirmationState& state,
                                           uint8_t startingSlaveId = 0,
                                           uint8_t maxBytes = EVENTS_REQUEST_MAX_BYTES)
    {
        Modbus::TRequest request({BROADCAST_ADDRESS, MODBUS_EXT_COMMAND, EVENTS_REQUEST_COMMAND});
        auto it = std::back_inserter(request);
        Append(it, startingSlaveId);
        Append(it, maxBytes);
        Append(it, state.SlaveId);
        Append(it, state.Flag);
        AppendBigEndian(it, CRC16::CalculateCRC16(request.data(), request.size()));
        return request;
    }

    TPort::TFrameCompletePred ExpectEvents()
    {
        return [=](uint8_t* buf, size_t size) {
            auto start = GetPacketStart(buf, size);
            if (start + SUB_COMMAND_POS >= size) {
                return false;
            }
            switch (buf[start + SUB_COMMAND_POS]) {
                case EVENTS_RESPONSE_COMMAND: {
                    if (size <= start + EVENTS_RESPONSE_HEADER_SIZE) {
                        return false;
                    }
                    return size ==
                           start + EVENTS_RESPONSE_HEADER_SIZE + buf[start + EVENTS_RESPONSE_DATA_SIZE_POS] + CRC_SIZE;
                }
                case NO_EVENTS_RESPONSE_COMMAND: {
                    return size == start + NO_EVENTS_RESPONSE_SIZE;
                }
                default:
                    // Unexpected sub command
                    return true;
            }
        };
    }

    void CheckCRC16(const uint8_t* packet, size_t size)
    {
        if (size < CRC_SIZE) {
            throw Modbus::TMalformedResponseError("invalid packet size: " + std::to_string(size));
        }

        auto crc = GetBigEndian<uint16_t>(packet + size - CRC_SIZE, packet + size);
        if (crc != CRC16::CalculateCRC16(packet, size - CRC_SIZE)) {
            throw Modbus::TInvalidCRCError();
        }
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
        port.WriteBytes(req);

        const auto timeout = GetTimeout(port);
        std::array<uint8_t, MAX_PACKET_SIZE + ARBITRATION_HEADER_MAX_BYTES> res;
        auto rc = port.ReadFrame(res.data(), res.size(), timeout, timeout, ExpectEvents());

        auto start = GetPacketStart(res.data(), res.size());
        auto packetSize = rc - start;
        const uint8_t* packet = res.data() + start;

        CheckCRC16(packet, packetSize);

        if (packet[COMMAND_POS] != MODBUS_EXT_COMMAND) {
            throw Modbus::TMalformedResponseError("invalid command");
        }

        switch (packet[SUB_COMMAND_POS]) {
            case EVENTS_RESPONSE_COMMAND: {
                if (EVENTS_RESPONSE_HEADER_SIZE + CRC_SIZE + packet[EVENTS_RESPONSE_DATA_SIZE_POS] != packetSize) {
                    throw Modbus::TMalformedResponseError("invalid data size");
                }
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
        auto rc = Port.ReadFrame(Response.data(), Request.size(), 8ms, FrameTimeout);

        CheckCRC16(Response.data(), rc);

        // Old firmwares can send any command with exception bit
        if (Response[COMMAND_POS] > 0x80) {
            throw TSerialDevicePermanentRegisterException("modbus exception, code " +
                                                          std::to_string(Response[EXCEPTION_CODE_POS]));
        }

        if (rc < MIN_ENABLE_EVENTS_RESPONSE_SIZE) {
            throw Modbus::TMalformedResponseError("invalid packet size: " + std::to_string(rc));
        }

        if (Response[SLAVE_ID_POS] != SlaveId) {
            throw Modbus::TMalformedResponseError("invalid slave id");
        }

        TBitIterator dataIt(Response.data() + ENABLE_EVENTS_RESPONSE_DATA_POS - 1,
                            Response[ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS]);

        TEventType lastType = TEventType::REBOOT;
        uint16_t lastRegAddr = 0;
        uint16_t lastDataAddr = 0;

        for (auto regIt = SettingsStart; regIt != SettingsEnd; ++regIt) {
            if (lastType != regIt->Type || lastRegAddr + MaxRegDistance < regIt->Addr) {
                dataIt.NextByte();
                lastDataAddr = regIt->Addr;
                lastType = regIt->Type;
                Visitor(lastType, lastDataAddr, dataIt.GetBit());
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
        Append(std::back_inserter(Request), {SlaveId, MODBUS_EXT_COMMAND, ENABLE_EVENTS_COMMAND, 0});
        Response.reserve(MAX_PACKET_SIZE);
        FrameTimeout = std::chrono::ceil<std::chrono::milliseconds>(port.GetSendTimeBytes(3.5));
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
        for (; HasSpaceForEnableEventRecord(Request) && regIt != Settings.cend(); ++regIt) {
            if (lastType != regIt->Type || lastAddr + MaxRegDistance < regIt->Addr) {
                Append(requestBack, static_cast<uint8_t>(regIt->Type));
                AppendBigEndian(requestBack, regIt->Addr);
                Append(requestBack, static_cast<uint8_t>(1));
                regCountPos = Request.size() - 1;
                Append(requestBack, static_cast<uint8_t>(regIt->Priority));
                Request[ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS] += MIN_ENABLE_EVENTS_REC_SIZE;
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
}
