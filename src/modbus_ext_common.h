#pragma once

#include "modbus_common.h"
#include "port.h"

namespace ModbusExt // modbus extension protocol common utilities
{
    enum TEventType : uint8_t
    {
        COIL = 1,
        DISCRETE = 2,
        HOLDING = 3,
        INPUT = 4,
        REBOOT = 15
    };

    enum TEventPriority : uint8_t
    {
        DISABLE = 0,
        LOW = 1,
        HIGH = 2
    };

    struct TEventConfirmationState
    {
        uint8_t SlaveId = 0;
        uint8_t Flag = 0;

        void Reset();
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

    /**
     * @brief Read events
     *
     * @return true - there are more events from devices
     * @return false - no more events
     */
    bool ReadEvents(TPort& port,
                    std::chrono::milliseconds maxReadingTime,
                    uint8_t startingSlaveId,
                    TEventConfirmationState& state,
                    IEventsVisitor& eventVisitor);

    //! Class builds packet for enabling events from specified registers
    class TEventsEnabler
    {
    public:
        typedef std::function<void(uint8_t, uint16_t, bool)> TVisitorFn;

        enum TEventsEnablerFlags
        {
            NO_HOLES,
            DISABLE_EVENTS_IN_HOLES
        };

        TEventsEnabler(uint8_t slaveId,
                       TPort& port,
                       TEventsEnabler::TVisitorFn visitor,
                       TEventsEnablerFlags flags = TEventsEnablerFlags::NO_HOLES);

        /**
         * @brief Add register to packet.
         *        If resulting packet is almost as big as 256 bytes,
         *        the class internally calls SendRequest and starts to build a new packet.
         *
         * @param addr register's address
         * @param type register's type
         * @param priority register's priority
         */
        void AddRegister(uint16_t addr, TEventType type, TEventPriority priority);

        /**
         * @brief Call the function to send a build packet
         *        The class parses answer and calls visitor for every register in answer,
         *        so one can know if events are enabled for specific register.
         */
        void SendRequests();

        bool HasEventsToSetup() const;

    private:
        struct TRegisterToEnable
        {
            TEventType Type;
            uint16_t Addr;
            TEventPriority Priority;
        };

        std::vector<uint8_t> Request;
        std::vector<uint8_t> Response;
        std::vector<TRegisterToEnable> Settings;
        std::vector<TRegisterToEnable>::const_iterator SettingsStart;
        std::vector<TRegisterToEnable>::const_iterator SettingsEnd;

        uint8_t SlaveId;
        TPort& Port;
        size_t MaxRegDistance;
        std::chrono::milliseconds FrameTimeout;
        TVisitorFn Visitor;

        void EnableEvents();
        void ClearRequest();

        void SendSingleRequest();
    };

    const uint8_t* GetPacketStart(const uint8_t* data, size_t size);

    class TModbusTraits: public Modbus::IModbusTraits
    {
        TPort::TFrameCompletePred ExpectNBytes(size_t n) const;

    public:
        TModbusTraits();

        size_t GetPacketSize(size_t pduSize) const override;

        void FinalizeRequest(Modbus::TRequest& request, uint8_t slaveId, uint32_t sn) override;

        TReadFrameResult ReadFrame(TPort& port,
                                   const std::chrono::milliseconds& responseTimeout,
                                   const std::chrono::milliseconds& frameTimeout,
                                   const Modbus::TRequest& req,
                                   Modbus::TResponse& resp) const override;

        uint8_t* GetPDU(std::vector<uint8_t>& frame) const override;
        const uint8_t* GetPDU(const std::vector<uint8_t>& frame) const override;
    };

} // modbus extension protocol common utilities
