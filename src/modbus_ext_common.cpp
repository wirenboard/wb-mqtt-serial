#include "modbus_ext_common.h"

#include "bin_utils.h"
#include "crc16.h"
#include "modbus_common.h"
#include <stddef.h>
#include <stdint.h>
#include <vector>

using namespace BinUtils;

namespace ModbusExt // modbus extension protocol declarations
{
    const uint8_t BROADCAST_ADDRESS = 0xFD;

    const uint8_t MODBUS_EXT_COMMAND = 0x60;

    const uint8_t EVENTS_REQUEST_COMMAND = 0x10;
    const uint8_t EVENTS_RESPONSE_COMMAND = 0x11;
    const uint8_t EVENTS_CLEAR_COMMAND = 0x12;
    const uint8_t EVENTS_CLEAR_RESPONSE_COMMAND = 0x13;
    const uint8_t NO_EVENTS_RESPONSE_COMMAND = 0x14;
    const uint8_t ENABLE_EVENTS_COMMAND = 0x18;
    const uint8_t ENABLE_EVENTS_RESPONSE_COMMAND = 0x1A;

    const uint32_t ALL_DEVICES_SN = 0;

    const size_t NO_EVENTS_RESPONSE_SIZE = 5;
    const size_t CLEAR_EVENTS_RESPONSE_SIZE = 10;
    const size_t EVENTS_RESPONSE_HEADER_SIZE = 10;
    const size_t CRC_SIZE = 2;
    const size_t MAX_PACKET_SIZE = 256;
    const size_t EVENT_HEADER_SIZE = 4;
    const size_t MIN_ENABLE_EVENTS_RESPONSE_SIZE = 7;
    const size_t ENABLE_EVENTS_REC_SIZE = 4;

    const uint8_t EVENTS_REQUEST_MAX_BYTES = MAX_PACKET_SIZE - EVENTS_RESPONSE_HEADER_SIZE - CRC_SIZE;
    const size_t ARBITRATION_HEADER_MAX_BYTES = 32;

    const size_t SUB_COMMAND_POS = 2;

    const size_t EVENTS_RESPONSE_SERIAL_NUMBER_POS = 3;
    const size_t EVENTS_RESPONSE_SLAVE_ID_POS = 7;
    const size_t EVENTS_RESPONSE_DATA_SIZE_POS = 9;
    const size_t EVENTS_RESPONSE_DATA_POS = 10;

    const size_t EVENT_SIZE_POS = 0;
    const size_t EVENT_TYPE_POS = 1;
    const size_t EVENT_ID_POS = 2;
    const size_t EVENT_DATA_POS = 4;

    const size_t ENABLE_EVENTS_RESPONSE_SLAVE_ID_POS = 3;
    const size_t ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS = 4;
    const size_t ENABLE_EVENTS_RESPONSE_DATA_POS = 5;

    const size_t ENABLE_EVENTS_REC_ADDR_POS = 0;
    const size_t ENABLE_EVENTS_REC_TYPE_POS = 2;
    const size_t ENABLE_EVENTS_REC_STATE_POS = 3;

    size_t GetPacketStart(const uint8_t* data, size_t size)
    {
        for (size_t i = 0; i < size; ++i) {
            if (data[i] != 0xFF) {
                return i;
            }
        }
        return size;
    }

    Modbus::TRequest MakeReadEventsRequest(uint32_t startingSerialNumber = ALL_DEVICES_SN,
                                           uint8_t maxBytes = EVENTS_REQUEST_MAX_BYTES)
    {
        Modbus::TRequest request({BROADCAST_ADDRESS, MODBUS_EXT_COMMAND, EVENTS_REQUEST_COMMAND});
        auto it = std::back_inserter(request);
        AppendBigEndian(it, startingSerialNumber);
        Append(it, maxBytes);
        AppendBigEndian(it, CRC16::CalculateCRC16(request.data(), request.size()));
        return request;
    }

    Modbus::TRequest MakeClearEventsRequest(const uint8_t* readEventsResponse, size_t size)
    {
        Modbus::TRequest request(size - CRC_SIZE);
        memcpy(request.data(), readEventsResponse, EVENTS_RESPONSE_SLAVE_ID_POS);
        request[SUB_COMMAND_POS] = EVENTS_CLEAR_COMMAND;
        memcpy(request.data() + EVENTS_RESPONSE_SLAVE_ID_POS,
               readEventsResponse + EVENTS_RESPONSE_DATA_SIZE_POS,
               size - CRC_SIZE - EVENTS_RESPONSE_DATA_SIZE_POS);
        AppendBigEndian(std::back_inserter(request), CRC16::CalculateCRC16(request.data(), request.size()));
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
                    return size == start + EVENTS_RESPONSE_HEADER_SIZE + buf[start + EVENTS_RESPONSE_DATA_SIZE_POS] + 2;
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

    void CheckPacket(const uint8_t* packet, size_t size, size_t minSize = CRC_SIZE)
    {
        if (size < minSize) {
            throw Modbus::TMalformedResponseError("invalid packet size: " + std::to_string(size));
        }
        auto crc = GetBigEndian<uint16_t>(packet + size - CRC_SIZE, packet + size);
        if (crc != CRC16::CalculateCRC16(packet, size - CRC_SIZE)) {
            throw Modbus::TInvalidCRCError();
        }
        if (packet[0] != BROADCAST_ADDRESS) {
            throw Modbus::TMalformedResponseError("invalid address");
        }
        if (packet[1] != MODBUS_EXT_COMMAND) {
            throw Modbus::TMalformedResponseError("invalid command");
        }
    }

    void IterateOverEvents(uint32_t serialNumber,
                           uint8_t slaveId,
                           const uint8_t* data,
                           size_t size,
                           IEventsVisitor& eventVisitor)
    {
        while (size != 0) {
            size_t dataSize = data[EVENT_SIZE_POS];
            size_t eventSize = dataSize + EVENT_HEADER_SIZE;
            if (eventSize > size) {
                throw Modbus::TMalformedResponseError("invalid event data size");
            }
            eventVisitor.Event(serialNumber,
                               slaveId,
                               data[EVENT_TYPE_POS],
                               GetFrom<uint16_t>(data + EVENT_ID_POS),
                               data + EVENT_DATA_POS,
                               dataSize);
            data += eventSize;
            size -= eventSize;
        }
    }

    void ClearEvents(TPort& port,
                     const std::chrono::milliseconds& responseTimeout,
                     const std::chrono::milliseconds& frameTimeout,
                     const uint8_t* readEventsResponse,
                     size_t size)
    {
        auto req = MakeClearEventsRequest(readEventsResponse, size);
        port.WriteBytes(req);
        std::array<uint8_t, CLEAR_EVENTS_RESPONSE_SIZE> res;
        auto rc = port.ReadFrame(res.data(), res.size(), responseTimeout + frameTimeout, frameTimeout);

        CheckPacket(res.data(), rc, res.size());

        if (res[SUB_COMMAND_POS] != EVENTS_CLEAR_RESPONSE_COMMAND) {
            throw Modbus::TMalformedResponseError("invalid clear events response command");
        }
    }

    bool ReadEvents(TPort& port,
                    const std::chrono::milliseconds& responseTimeout,
                    const std::chrono::milliseconds& frameTimeout,
                    IEventsVisitor& eventVisitor)
    {
        auto req = MakeReadEventsRequest();
        port.WriteBytes(req);

        std::array<uint8_t, MAX_PACKET_SIZE + ARBITRATION_HEADER_MAX_BYTES> res;
        auto rc = port.ReadFrame(res.data(), res.size(), responseTimeout + frameTimeout, frameTimeout, ExpectEvents());

        auto start = GetPacketStart(res.data(), res.size());
        auto packetSize = rc - start;
        const uint8_t* packet = res.data() + start;

        CheckPacket(packet, packetSize, NO_EVENTS_RESPONSE_SIZE);

        switch (packet[SUB_COMMAND_POS]) {
            case EVENTS_RESPONSE_COMMAND: {
                if (EVENTS_RESPONSE_HEADER_SIZE + CRC_SIZE + packet[EVENTS_RESPONSE_DATA_SIZE_POS] != packetSize) {
                    throw Modbus::TMalformedResponseError("invalid data size");
                }
                IterateOverEvents(GetFromBigEndian<uint32_t>(packet + EVENTS_RESPONSE_SERIAL_NUMBER_POS),
                                  packet[EVENTS_RESPONSE_SLAVE_ID_POS],
                                  packet + EVENTS_RESPONSE_DATA_POS,
                                  packet[EVENTS_RESPONSE_DATA_SIZE_POS],
                                  eventVisitor);
                ClearEvents(port, responseTimeout, frameTimeout, packet, packetSize);
                return true;
            }
            case NO_EVENTS_RESPONSE_COMMAND: {
                return false;
            }
            default: {
                throw Modbus::TMalformedResponseError("invalid command");
            }
        }
    }

    void TEventsEnabler::EnableEvents()
    {
        Port.WriteBytes(Request);

        auto rc = Port.ReadFrame(Response.data(), Request.size(), ResponseTimeout + FrameTimeout, FrameTimeout);

        CheckPacket(Response.data(), rc, MIN_ENABLE_EVENTS_RESPONSE_SIZE);
        if (Response[SUB_COMMAND_POS] != ENABLE_EVENTS_RESPONSE_COMMAND) {
            throw Modbus::TMalformedResponseError("invalid enable events response command");
        }
        if (Response[ENABLE_EVENTS_RESPONSE_SLAVE_ID_POS] != SlaveId) {
            throw Modbus::TMalformedResponseError("invalid slave id");
        }

        const uint8_t* data = Response.data() + ENABLE_EVENTS_RESPONSE_DATA_POS;
        const uint8_t* end = data + Response[ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS];
        for (; data + ENABLE_EVENTS_REC_SIZE <= end; data += ENABLE_EVENTS_REC_SIZE) {
            Visitor(GetFrom<uint16_t>(data),
                    static_cast<TEventRegisterType>(data[ENABLE_EVENTS_REC_TYPE_POS]),
                    data[ENABLE_EVENTS_REC_STATE_POS]);
        }
    }

    void TEventsEnabler::ClearRequest()
    {
        Request.erase(Request.begin() + ENABLE_EVENTS_RESPONSE_DATA_POS, Request.end());
        Request[ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS] = 0;
    }

    TEventsEnabler::TEventsEnabler(uint8_t slaveId,
                                   TPort& port,
                                   const std::chrono::milliseconds& responseTimeout,
                                   const std::chrono::milliseconds& frameTimeout,
                                   TEventsEnabler::TVisitorFn visitor)
        : SlaveId(slaveId),
          Port(port),
          ResponseTimeout(responseTimeout),
          FrameTimeout(frameTimeout),
          Visitor(visitor)
    {
        Request.reserve(MAX_PACKET_SIZE);
        Append(std::back_inserter(Request), {BROADCAST_ADDRESS, MODBUS_EXT_COMMAND, ENABLE_EVENTS_COMMAND, SlaveId, 0});
        Response.reserve(MAX_PACKET_SIZE);
    }

    void TEventsEnabler::AddRegister(uint16_t addr, TEventRegisterType type)
    {
        auto it = std::back_inserter(Request);
        Append(it, addr);
        Append(it, {type, 1});
        Request[ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS] += ENABLE_EVENTS_REC_SIZE;
        if (Request.size() + CRC_SIZE + ENABLE_EVENTS_REC_SIZE > Request.capacity()) {
            SendRequest();
        }
    }

    void TEventsEnabler::SendRequest()
    {
        if (Request[ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS] != 0) {
            AppendBigEndian(std::back_inserter(Request), CRC16::CalculateCRC16(Request.data(), Request.size()));
            try {
                EnableEvents();
                ClearRequest();
            } catch (...) {
                ClearRequest();
                throw;
            }
        }
    }

}
