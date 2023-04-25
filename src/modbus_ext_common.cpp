#include "modbus_ext_common.h"

#include "bin_utils.h"
#include "crc16.h"
#include "log.h"
#include "modbus_common.h"
#include <stddef.h>
#include <stdint.h>
#include <vector>

using namespace BinUtils;

#define LOG(logger) logger.Log() << "[modbus-ext] "

namespace ModbusExt // modbus extension protocol declarations
{
    const uint8_t BROADCAST_ADDRESS = 0xFD;

    const uint8_t MODBUS_EXT_COMMAND = 0x46;

    // Function codes
    const uint8_t EVENTS_REQUEST_COMMAND = 0x10;
    const uint8_t EVENTS_RESPONSE_COMMAND = 0x11;
    const uint8_t NO_EVENTS_RESPONSE_COMMAND = 0x14;
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

    bool IsRegisterEvent(TEventType type)
    {
        return type == TEventType::COIL || type == TEventType::DISCRETE || type == TEventType::HOLDING ||
               type == TEventType::INPUT;
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
        auto timePerByte = port.GetSendTime(1);
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
                    std::chrono::milliseconds responseTimeout,
                    std::chrono::milliseconds frameTimeout,
                    std::chrono::milliseconds maxEventsReadTime,
                    uint8_t startingSlaveId,
                    TEventConfirmationState& state,
                    IEventsVisitor& eventVisitor)
    {
        // TODO: Count request and arbitration
        auto maxBytes = GetMaxReadEventsResponseSize(port, maxEventsReadTime);

        auto req = MakeReadEventsRequest(state, startingSlaveId, maxBytes);
        port.WriteBytes(req);

        std::array<uint8_t, MAX_PACKET_SIZE + ARBITRATION_HEADER_MAX_BYTES> res;
        auto rc = port.ReadFrame(res.data(), res.size(), responseTimeout + frameTimeout, frameTimeout, ExpectEvents());

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

    void TEventsEnabler::EnableEvents()
    {
        Port.WriteBytes(Request);

        auto rc = Port.ReadFrame(Response.data(), Request.size(), ResponseTimeout + FrameTimeout, FrameTimeout);

        CheckCRC16(Response.data(), rc);

        if (Response[COMMAND_POS] == Request[COMMAND_POS] + 0x80) {
            throw TSerialDevicePermanentRegisterException("modbus exception, code " +
                                                          std::to_string(Response[EXCEPTION_CODE_POS]));
        }

        if (rc < MIN_ENABLE_EVENTS_RESPONSE_SIZE) {
            throw Modbus::TMalformedResponseError("invalid packet size: " + std::to_string(rc));
        }

        if (Response[SLAVE_ID_POS] != SlaveId) {
            throw Modbus::TMalformedResponseError("invalid slave id");
        }

        const uint8_t* data = Response.data() + ENABLE_EVENTS_RESPONSE_DATA_POS;
        const uint8_t* end = data + Response[ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS];

        for (const auto& reg: Settings) {
            auto dataSize = (reg.second.size() + 7) / 8;
            auto type = reg.first.first;
            auto addr = reg.first.second;
            for (size_t i = 0; i < reg.second.size(); ++i) {
                if (dataSize > 0 && data + dataSize <= end) {
                    std::bitset<8> bits(data[i / 8]);
                    Visitor(type, addr + i, bits.test(7 - (8 + i) % 8));
                }
            }
            data += dataSize;
        }
    }

    void TEventsEnabler::ClearRequest()
    {
        Request.erase(Request.begin() + ENABLE_EVENTS_RESPONSE_DATA_POS, Request.end());
        Request[ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS] = 0;
    }

    TEventsEnabler::TEventsEnabler(uint8_t slaveId,
                                   TPort& port,
                                   std::chrono::milliseconds responseTimeout,
                                   std::chrono::milliseconds frameTimeout,
                                   TEventsEnabler::TVisitorFn visitor)
        : SlaveId(slaveId),
          Port(port),
          ResponseTimeout(responseTimeout),
          FrameTimeout(frameTimeout),
          Visitor(visitor)
    {
        Request.reserve(MAX_PACKET_SIZE);
        Append(std::back_inserter(Request), {SlaveId, MODBUS_EXT_COMMAND, ENABLE_EVENTS_COMMAND, 0});
        Response.reserve(MAX_PACKET_SIZE);
    }

    void TEventsEnabler::AddRegister(uint16_t addr, TEventType type, TEventPriority priority)
    {
        if (!IsRegisterEvent(type)) {
            return;
        }
        auto it = std::find_if(Settings.begin(), Settings.end(), [type, addr](const auto& v) {
            auto nextAddr = v.first.second + static_cast<uint16_t>(v.second.size());
            return v.first.first == type && nextAddr == addr;
        });
        if (it != Settings.end()) {
            it->second.push_back(priority);
            Request[ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS] += 1;
        } else {
            Settings[std::make_pair(type, addr)].push_back(priority);
            Request[ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS] += MIN_ENABLE_EVENTS_REC_SIZE;
        }

        if (Request.size() + Request[ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS] + CRC_SIZE + MIN_ENABLE_EVENTS_REC_SIZE >
            Request.capacity())
        {
            SendRequest();
        }
    }

    void TEventsEnabler::SendRequest()
    {
        if (Request[ENABLE_EVENTS_RESPONSE_DATA_SIZE_POS] == 0) {
            return;
        }

        auto it = std::back_inserter(Request);
        for (const auto& reg: Settings) {
            auto type = reg.first.first;
            auto addr = reg.first.second;

            Append(it, static_cast<uint8_t>(type));
            AppendBigEndian(it, addr);
            Append(it, static_cast<uint8_t>(reg.second.size()));

            for (const auto& priority: reg.second) {
                Append(it, static_cast<uint8_t>(priority));
            }
        }

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
