#pragma once

#include "port.h"

namespace ModbusExt // modbus extension protocol common utilities
{
    class IEventsVisitor
    {
    public:
        virtual ~IEventsVisitor() = default;

        virtual void Event(uint32_t serialNumber,
                           uint8_t slaveId,
                           uint8_t eventType,
                           uint16_t eventId,
                           const uint8_t* data,
                           size_t dataSize) = 0;
    };

    bool ReadEvents(TPort& port,
                    const std::chrono::milliseconds& responseTimeout,
                    const std::chrono::milliseconds& frameTimeout,
                    IEventsVisitor& eventVisitor);

} // modbus extension protocol common utilities
