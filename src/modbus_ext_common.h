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

    enum TEventPriority : uint8_t
    {
        DISABLE = 0,
        LOW = 1,
        HIGH = 2
    };

    struct TEventConfirmationState
    {
        uint8_t SlaveId;
        uint8_t Flag;
    };

    class IEventsVisitor
    {
    public:
        virtual ~IEventsVisitor() = default;

        virtual void Event(uint8_t slaveId,
                           uint8_t eventType,
                           uint16_t eventId,
                           const uint8_t* data,
                           size_t dataSize) = 0;
    };

    bool ReadEvents(TPort& port,
                    const std::chrono::milliseconds& responseTimeout,
                    const std::chrono::milliseconds& frameTimeout,
                    IEventsVisitor& eventVisitor,
                    TEventConfirmationState& state);

    //! Class builds packet for enabling events from specified registers
    class TEventsEnabler
    {
    public:
        typedef std::function<void(uint16_t, bool)> TVisitorFn;

        TEventsEnabler(uint8_t slaveId,
                       TPort& port,
                       const std::chrono::milliseconds& responseTimeout,
                       const std::chrono::milliseconds& frameTimeout,
                       TEventsEnabler::TVisitorFn visitor);

        /**
         * @brief Add register to packet.
         *        If resulting packet is almost as big as 256 bytes,
         *        the class internally calls SendRequest and starts to build a new packet.
         *
         * @param addr register's address
         * @param type register's type
         * @param priority register's priority
         */
        void AddRegister(uint16_t addr, TEventRegisterType type, TEventPriority priority);

        /**
         * @brief Call the function to send a build packet
         *        The class parses answer and calls visitor for every register in answer,
         *        so one can know if events are enabled for specific register.
         */
        void SendRequest();

    private:
        std::vector<uint8_t> Request;
        std::vector<uint8_t> Response;
        std::vector<uint8_t> Registers;

        uint8_t SlaveId;
        TPort& Port;
        const std::chrono::milliseconds& ResponseTimeout;
        const std::chrono::milliseconds& FrameTimeout;
        TVisitorFn Visitor;

        void EnableEvents();
        void ClearRequest();
    };

} // modbus extension protocol common utilities
