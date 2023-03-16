#pragma once

#include "port.h"

namespace ModbusExt // modbus extension protocol common utilities
{
    enum TEventRegisterType : uint8_t
    {
        COIL = 1,
        DISCRETE = 2,
        HOLDING = 3,
        INPUT = 4
    };

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

    class TEventsEnabler
    {
    public:
        typedef std::function<void(uint16_t, TEventRegisterType, uint8_t)> TVisitorFn;

        TEventsEnabler(uint8_t slaveId,
                       TPort& port,
                       const std::chrono::milliseconds& responseTimeout,
                       const std::chrono::milliseconds& frameTimeout,
                       TEventsEnabler::TVisitorFn visitor);

        void Enable(uint16_t addr, TEventRegisterType type);
        void Finalize();

    private:
        std::vector<uint8_t> Request;
        std::vector<uint8_t> Response;

        uint8_t SlaveId;
        TPort& Port;
        const std::chrono::milliseconds& ResponseTimeout;
        const std::chrono::milliseconds& FrameTimeout;
        TVisitorFn Visitor;

        void EnableEvents();
        void ClearRequest();
    };

} // modbus extension protocol common utilities
