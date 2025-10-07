#pragma once

#include <optional>

#include "modbus_base.h"
#include "port/port.h"

namespace ModbusExt // modbus extension protocol common utilities
{
    enum TModbusExtCommand : uint8_t
    {
        ACTUAL = 0x46,
        DEPRECATED = 0x60
    };

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

    bool IsRegisterEvent(uint8_t eventType);

    /**
     * @brief Read events
     *
     * @return true - there are more events from devices
     * @return false - no more events
     */
    bool ReadEvents(TPort& port,
                    Modbus::IModbusTraits& traits,
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
                       Modbus::IModbusTraits& traits,
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

        std::vector<TRegisterToEnable> Settings;
        std::vector<TRegisterToEnable>::const_iterator SettingsStart;
        std::vector<TRegisterToEnable>::const_iterator SettingsEnd;

        uint8_t SlaveId;
        TPort& Port;
        Modbus::IModbusTraits& Traits;
        size_t MaxRegDistance;
        std::chrono::milliseconds FrameTimeout;
        TVisitorFn Visitor;

        void EnableEvents(const std::vector<uint8_t>& requestPdu);

        void SendSingleRequest();
    };

    const uint8_t* GetRTUPacketStart(const uint8_t* data, size_t size);

    class TModbusTraits: public Modbus::IModbusTraits
    {
        uint32_t Sn;
        TModbusExtCommand ModbusExtCommand;
        std::unique_ptr<Modbus::IModbusTraits> BaseTraits;

        size_t GetIntermediatePduSize(size_t pduSize) const;
        void FinalizeIntermediatePdu(std::vector<uint8_t>& request);
        void ValidateIntermediateResponsePdu(const std::vector<uint8_t>& response) const;

    public:
        TModbusTraits(std::unique_ptr<Modbus::IModbusTraits> baseTraits);

        Modbus::TReadResult Transaction(TPort& port,
                                        uint8_t slaveId,
                                        const std::vector<uint8_t>& requestPdu,
                                        size_t expectedResponsePduSize,
                                        const std::chrono::milliseconds& responseTimeout,
                                        const std::chrono::milliseconds& frameTimeout,
                                        bool matchSlaveId = true) override;

        void SetSn(uint32_t sn);
        void SetModbusExtCommand(TModbusExtCommand command);
    };

    class TModbusRTUWithArbitrationTraits: public Modbus::IModbusTraits
    {
        TPort::TFrameCompletePred ExpectFastModbusRTU() const;

        void FinalizeRequest(std::vector<uint8_t>& request, uint8_t slaveId);

    public:
        TModbusRTUWithArbitrationTraits();

        Modbus::TReadResult Transaction(TPort& port,
                                        uint8_t slaveId,
                                        const std::vector<uint8_t>& requestPdu,
                                        size_t expectedResponsePduSize,
                                        const std::chrono::milliseconds& responseTimeout,
                                        const std::chrono::milliseconds& frameTimeout,
                                        bool matchSlaveId = true) override;
    };

    struct TScannedDevice
    {
        uint8_t SlaveId;
        uint32_t Sn;
    };

    /**
     * Scans the specified port for devices using Fast Modbus.
     * Throws an exception if an error occurs.
     * The scannedDevices vector will contain devices found before error.
     *
     * @param port The port to scan.
     * @param modbusExtCommand The Fast Modbus command to use for scanning.
     * @param scannedDevices A vector to store the scanned devices.
     */
    void Scan(TPort& port,
              Modbus::IModbusTraits& traits,
              ModbusExt::TModbusExtCommand modbusExtCommand,
              std::vector<ModbusExt::TScannedDevice>& scannedDevices);

    /**
     * Starts scan on specified port for devices using Fast Modbus.
     * Throws an exception if an error occurs.
     *
     * @param port The port to scan.
     * @param modbusExtCommand The Fast Modbus command to use for scanning.
     * @return The scanned device or std::nullopt if no devices found.
     */
    std::optional<ModbusExt::TScannedDevice> ScanStart(TPort& port,
                                                       Modbus::IModbusTraits& traits,
                                                       ModbusExt::TModbusExtCommand modbusExtCommand);

    /**
     * Continues scan on specified port for devices using Fast Modbus.
     * Throws an exception if an error occurs.
     *
     * @param port The port to scan.
     * @param modbusExtCommand The Fast Modbus command to use for scanning.
     * @return The scanned device or std::nullopt if no more devices found.
     */
    std::optional<ModbusExt::TScannedDevice> ScanNext(TPort& port,
                                                      Modbus::IModbusTraits& traits,
                                                      ModbusExt::TModbusExtCommand modbusExtCommand);

} // modbus extension protocol common utilities
