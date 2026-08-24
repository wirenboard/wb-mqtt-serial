
#include "fake_serial_port.h"
#include "log.h"
#include "modbus_expectations_base.h"
#include "serial_driver.h"

using namespace std::chrono_literals;
using namespace std::chrono;

namespace
{
    const uint8_t MAX_EVENT_RESPONSE_SIZE = 0xF8;

    const auto DISCONNECTED_POLL_DELAY_STEP = 500ms;
    const auto DISCONNECTED_POLL_DELAY_LIMIT = 10s;

    // 2028-02-29 23:59:59 UTC, fake system time of time synchronization tests
    const time_t FAKE_SYSTEM_TIME = 1835481599;

    // Modbus exception code that is not treated as unsupported register
    const uint8_t SLAVE_DEVICE_FAILURE = 0x04;

    // Devices are set to local time, not to UTC. POSIX form of UTC+3 is used to avoid dependency on tzdata
    const char* FAKE_TIMEZONE = "MSK-3";
    const time_t FAKE_LOCAL_TIME = FAKE_SYSTEM_TIME + duration_cast<seconds>(3h).count();

    class TTimeMock
    {
        steady_clock::time_point Time;

    public:
        void Reset()
        {
            Time = steady_clock::time_point();
        }

        steady_clock::time_point GetTime() const
        {
            return Time;
        }

        void AddTime(microseconds intervalToAdd)
        {
            Time += intervalToAdd;
        }
    };

    class TFakeSerialPortWithTime: public TFakeSerialPort
    {
        std::deque<microseconds> FrameReadTimes;
        TTimeMock& TimeMock;

    public:
        TFakeSerialPortWithTime(WBMQTT::Testing::TLoggedFixture& fixture, TTimeMock& timeMock)
            : TFakeSerialPort(fixture, "<TFakeSerialPortWithTime>"),
              TimeMock(timeMock)
        {}

        using TFakeSerialPort::Expect;

        void Expect(const std::vector<int>& request,
                    const std::vector<int>& response,
                    const char* func,
                    microseconds frameReadTime)
        {
            TFakeSerialPort::Expect(request, response, func);
            FrameReadTimes.push_back(frameReadTime);
        }

        TReadFrameResult ReadFrame(uint8_t* buf,
                                   size_t count,
                                   const microseconds& responseTimeout = -1ms,
                                   const microseconds& frameTimeout = -1ms,
                                   TFrameCompletePred frame_complete = 0) override
        {
            if (!FrameReadTimes.empty()) {
                TimeMock.AddTime(FrameReadTimes.front());
                FrameReadTimes.pop_front();
            }
            return TFakeSerialPort::ReadFrame(buf, count, responseTimeout, frameTimeout, frame_complete);
        }

        void SleepSinceLastInteraction(const std::chrono::microseconds& us) override
        {
            TFakeSerialPort::SleepSinceLastInteraction(us);
            TimeMock.AddTime(us);
        }
    };
}

class TPollTest: public TLoggedFixture, public TModbusExpectationsBase
{
public:
    void SetUp() override
    {
        TLoggedFixture::SetUp();
        setenv("TZ", FAKE_TIMEZONE, 1);
        tzset();
        TimeMock.Reset();
        SystemTime = system_clock::from_time_t(FAKE_SYSTEM_TIME);
        Port = std::make_shared<TFakeSerialPortWithTime>(*this, TimeMock);
        FeaturePort = std::make_shared<TFeaturePort>(Port, false);
        TModbusDevice::Register(DeviceFactory);
        FeaturePort->Open();
    }

    void TearDown() override
    {
        FeaturePort->Close();
        unsetenv("TZ");
        tzset();
        TLoggedFixture::TearDown();
    }

    void Cycle(TSerialClientRegisterAndEventsReader& serialClient, TSerialClientDeviceAccessHandler& lastAccessedDevice)
    {
        Emit() << ceil<microseconds>(TimeMock.GetTime().time_since_epoch()).count() << ": Cycle";
        serialClient.OpenPortCycle(
            *FeaturePort,
            [this](PRegister reg) {
                std::string errorMsg;
                if (reg->GetErrorState().any()) {
                    errorMsg = " with errors: " + reg->GetErrorState().to_string();
                }
                Emit() << ceil<microseconds>(TimeMock.GetTime().time_since_epoch()).count() << ": " << reg->ToString()
                       << errorMsg;
            },
            lastAccessedDevice);
        auto deadline = serialClient.GetDeadline(TimeMock.GetTime());
        if (deadline > TimeMock.GetTime()) {
            TimeMock.AddTime(ceil<microseconds>(deadline - TimeMock.GetTime()));
        }
        Emit() << ceil<microseconds>(TimeMock.GetTime().time_since_epoch()).count() << ": Cycle end\n";
    }

    void EnqueueEnableContinuousRead(uint8_t slaveId, microseconds readTime)
    {
        SetModbusRTUSlaveId(slaveId);
        Port->Expect(WrapPDU({
                         0x03, // function code
                         0x00, // starting address Hi
                         0x72, // starting address Lo
                         0x00, // quantity Hi
                         0x01  // quantity Lo
                     }),
                     WrapPDU({
                         0x03, // function code
                         0x02, // byte count
                         0x00, // data Hi
                         0x00  // data Lo
                     }),
                     __func__,
                     readTime);
        Port->Expect(WrapPDU({
                         0x06, // function code
                         0x00, // starting address Hi
                         0x72, // starting address Lo
                         0x00, // value Hi
                         0x01  // value Lo
                     }),
                     WrapPDU({
                         0x06, // function code
                         0x00, // starting address Hi
                         0x72, // starting address Lo
                         0x00, // value Hi
                         0x01  // value Lo
                     }),
                     __func__,
                     readTime);
    }

    void EnqueueWriteLocalTime(uint8_t slaveId, uint64_t localTime, microseconds readTime, uint8_t exceptionCode = 0)
    {
        SetModbusRTUSlaveId(slaveId);
        std::vector<int> request = {
            0x10, // function code
            0x01, // starting address Hi
            0xC4, // starting address Lo
            0x00, // quantity Hi
            0x04, // quantity Lo
            0x08  // byte count
        };
        for (auto i = 0; i < 8; ++i) {
            request.push_back((localTime >> (56 - i * 8)) & 0xFF);
        }
        Port->Expect(WrapPDU(request),
                     exceptionCode ? WrapPDU({
                                         0x90,         // function code + exception bit
                                         exceptionCode //
                                     })
                                   : WrapPDU({
                                         0x10, // function code
                                         0x01, // starting address Hi
                                         0xC4, // starting address Lo
                                         0x00, // quantity Hi
                                         0x04  // quantity Lo
                                     }),
                     __func__,
                     readTime);
    }

    void EnqueueReadHolding(uint8_t slaveId, uint16_t addr, uint16_t count, microseconds readTime)
    {
        SetModbusRTUSlaveId(slaveId);
        std::vector<int> response = {0x03, count * 2};
        for (auto i = 0; i < count; ++i) {
            response.push_back((i >> 8) & 0xFF);
            response.push_back(i & 0xFF);
        }
        Port->Expect(WrapPDU({
                         0x03,                // function code
                         (addr >> 8) & 0xFF,  // starting address Hi
                         addr & 0xFF,         // starting address Lo
                         (count >> 8) & 0xFF, // quantity Hi
                         count & 0xFF         // quantity Lo
                     }),
                     WrapPDU(response),
                     __func__,
                     readTime);
    }

    void EnqueueReadHoldingError(uint8_t slaveId, uint16_t addr, uint16_t count, microseconds readTime)
    {
        SetModbusRTUSlaveId(slaveId);
        Port->Expect(WrapPDU({
                         0x03,                // function code
                         (addr >> 8) & 0xFF,  // starting address Hi
                         addr & 0xFF,         // starting address Lo
                         (count >> 8) & 0xFF, // quantity Hi
                         count & 0xFF         // quantity Lo
                     }),
                     std::vector<int>(),
                     __func__,
                     readTime);
    }

    void EnqueueReadEvents(microseconds readTime, uint8_t slaveId, bool error, uint8_t responseSize)
    {
        SetModbusRTUSlaveId(0xFD);
        Port->Expect(WrapPDU({
                         0x46,         // function code
                         0x10,         // subcommand
                         slaveId,      // staring slaveId
                         responseSize, // max response size
                         slaveId,      //
                         0x00          //
                     }),
                     WrapPDU({
                         0x46,               // function code
                         error ? 0x14 : 0x12 // subcommand, no events
                     }),
                     __func__,
                     readTime);
    }

    void EnqueueReadEvents(microseconds readTime)
    {
        EnqueueReadEvents(readTime, 0, false, MAX_EVENT_RESPONSE_SIZE);
    }

    void EnqueueResetEvent(microseconds readTime, uint8_t slaveId)
    {
        SetModbusRTUSlaveId(0xFD);
        Port->Expect(WrapPDU({
                         0x46,                    // function code
                         0x10,                    // subcommand
                         0x00,                    // staring slaveId
                         MAX_EVENT_RESPONSE_SIZE, // response size
                         0x00,                    //
                         0x00                     //
                     }),
                     WrapPDU(
                         {
                             0x46, // function code
                             0x11, // subcommand, events
                             0x00, // flag
                             0x01, // event count
                             0x04, // data size
                             0x00, // additional data size
                             0x0f, // event type: reset
                             0x00, // event id
                             0x00, // event id
                         },
                         slaveId),
                     __func__,
                     readTime);
    }

    void EnqueueEnableEvents(uint8_t slaveId,
                             uint16_t addr,
                             microseconds readTime,
                             uint8_t res = 0x01,
                             bool error = false)
    {
        SetModbusRTUSlaveId(slaveId);
        Port->Expect(WrapPDU({
                         0x46,               // function code
                         0x18,               // subcommand
                         0x0A,               // data size
                         0x03,               // holding
                         (addr >> 8) & 0xFF, // address Hi
                         addr & 0xFF,        // address Lo
                         0x01,               // count
                         0x01,               // enable
                         0x0F,               // disable reset event
                         0x00,               //
                         0x00,               //
                         0x01,               //
                         0x00                //
                     }),
                     WrapPDU({
                         0x46,                // function code
                         error ? 0x14 : 0x18, // subcommand
                         0x02,                // data size
                         res,                 //
                         0x00                 //
                     }),
                     __func__,
                     readTime);
    }

    // Full control over an EVENTS_REQUEST (0x46/0x10): explicit min_slave and confirmation
    // state in the request plus an arbitrary response. Used to drive the read-cap logic that
    // the simpler EnqueueReadEvents helpers cannot express.
    void EnqueueEventsExchange(microseconds readTime,
                               uint8_t minSlave,
                               uint8_t confirmSlave,
                               uint8_t confirmFlag,
                               const std::vector<int>& responsePdu,
                               uint8_t responderSlaveId)
    {
        SetModbusRTUSlaveId(0xFD);
        Port->Expect(WrapPDU({
                         0x46,                    // function code
                         0x10,                    // subcommand
                         minSlave,                // starting slaveId
                         MAX_EVENT_RESPONSE_SIZE, // max response size
                         confirmSlave,            // confirmed slaveId
                         confirmFlag              // confirmed flag
                     }),
                     WrapPDU(responsePdu, responderSlaveId),
                     __func__,
                     readTime);
    }

    // HAS_EVENTS response PDU carrying one holding-register change event.
    std::vector<int> HoldingEventResponse(uint8_t flag, uint16_t addr, uint16_t value)
    {
        return {
            0x46,                // function code
            0x11,                // subcommand: has events
            flag,                // confirmation flag
            0x01,                // event count
            0x06,                // events data length
            0x02,                // event data size
            0x03,                // event type: holding
            (addr >> 8) & 0xFF,  // event id (register address) Hi
            addr & 0xFF,         // event id Lo
            (value >> 8) & 0xFF, // value Hi
            value & 0xFF         // value Lo
        };
    }

    // NO_EVENTS response PDU (sent by a device with the broadcast address 0xFD).
    std::vector<int> NoEventsResponse()
    {
        return {0x46, 0x12};
    }

    PExpector Expector() const override
    {
        return Port;
    }

    TModbusDeviceConfig MakeDeviceConfig(const std::string& name, const std::string& addr)
    {
        TModbusDeviceConfig config;
        config.CommonConfig = std::make_shared<TDeviceConfig>(name, addr, "modbus");
        config.CommonConfig->FrameTimeout = 0ms;
        return config;
    }

    void AddRegister(TSerialDevice& device,
                     uint16_t addr,
                     milliseconds readPeriod = 0ms,
                     TRegisterConfig::TSporadicMode sporadicMode = TRegisterConfig::TSporadicMode::DISABLED)
    {
        auto registerConfig = TRegisterConfig::Create(Modbus::REG_HOLDING, addr);
        if (readPeriod != 0ms) {
            registerConfig->ReadPeriod = readPeriod;
        }
        if (sporadicMode == TRegisterConfig::TSporadicMode::DISABLED) {
            device.SetSporadicOnly(false);
        }
        registerConfig->SporadicMode = sporadicMode;
        device.AddRegister(registerConfig);
    }

    std::shared_ptr<TModbusDevice> MakeDevice(const TModbusDeviceConfig& config)
    {
        return std::make_shared<TModbusDevice>(std::make_unique<Modbus::TModbusRTUTraits>(),
                                               config,
                                               DeviceFactory.GetProtocol("modbus"));
    }

    std::shared_ptr<TFakeSerialPortWithTime> Port;
    std::shared_ptr<TFeaturePort> FeaturePort;
    TTimeMock TimeMock;
    system_clock::time_point SystemTime;
    TSerialDeviceFactory DeviceFactory;
};

TEST_F(TPollTest, SingleDeviceSingleRegister)
{
    // One register without fixed read period and without events support
    // Check what poller reads it as soon as possible

    Port->SetBaudRate(115200);
    auto config = MakeDeviceConfig("device1", "1");
    auto device = MakeDevice(config);
    AddRegister(*device, 1);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    for (size_t i = 0; i < 10; ++i) {
        EnqueueReadHolding(1, 1, 1, 30ms);
        Cycle(serialClient, lastAccessedDevice);
    }
}

TEST_F(TPollTest, SingleDeviceSingleRegisterWithReadPeriod)
{
    // One register with fixed read period
    // Check what read period is preserved

    Port->SetBaudRate(115200);
    auto config = MakeDeviceConfig("device1", "1");
    auto device = MakeDevice(config);
    AddRegister(*device, 1, 100ms);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    for (size_t i = 0; i < 10; ++i) {
        EnqueueReadHolding(1, 1, 1, 30ms);
        Cycle(serialClient, lastAccessedDevice);
    }
}

TEST_F(TPollTest, SingleDeviceSeveralRegisters)
{
    // One register with fixed read period and one without it
    // Check what read period is preserved and poller wait for the first register,
    // if there are not enough time to read the second register

    Port->SetBaudRate(115200);
    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->RequestDelay = 10ms;
    auto device = MakeDevice(config);
    AddRegister(*device, 1);
    AddRegister(*device, 2, 50ms);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    for (size_t i = 0; i < 10; ++i) {
        EnqueueReadHolding(1, 2, 1, 10ms);
        Cycle(serialClient, lastAccessedDevice);
        EnqueueReadHolding(1, 1, 1, 10ms);
        Cycle(serialClient, lastAccessedDevice);

        // Not enough time before reading register with read period
        Cycle(serialClient, lastAccessedDevice);
    }
}

TEST_F(TPollTest, SingleDeviceSingleRegisterWithEvents)
{
    // One register with events
    // 1. Events must be enabled
    // 2. Read once by normal request
    // 3. Events read requests are sent every 50ms (10 times)
    // 4. The register is read again by normal request according to "sporadic only" device polling

    Port->SetBaudRate(115200);
    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->RequestDelay = 10ms;
    auto device = MakeDevice(config);
    AddRegister(*device, 1, 0ms, TRegisterConfig::TSporadicMode::ONLY_EVENTS);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    // Read registers
    EnqueueEnableEvents(1, 1, 10ms);
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    // Only read events
    for (size_t i = 0; i < 10; ++i) {
        EnqueueReadEvents(4ms);
        Cycle(serialClient, lastAccessedDevice);
    }

    // Read registers
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);
}

TEST_F(TPollTest, SingleDeviceSingleRegisterWithEventsAndPolling)
{
    // One register with events and one without read period
    // 1. Events must be enabled
    // 2. The register is read once by normal request and excluded from polling
    // 3. Events read requests are sent every 50ms
    // 4. Register without read period must be polled during free time

    Port->SetBaudRate(115200);
    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->RequestDelay = 10ms;
    auto device = MakeDevice(config);
    AddRegister(*device, 1, 0ms, TRegisterConfig::TSporadicMode::ONLY_EVENTS);
    AddRegister(*device, 2);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    // Enable events and read first register
    EnqueueEnableEvents(1, 1, 10ms);
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);
    EnqueueReadHolding(1, 2, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    for (size_t i = 0; i < 3; ++i) {
        EnqueueReadEvents(4ms);
        Cycle(serialClient, lastAccessedDevice);
        EnqueueReadHolding(1, 2, 1, 10ms);
        Cycle(serialClient, lastAccessedDevice);
        EnqueueReadHolding(1, 2, 1, 10ms);
        Cycle(serialClient, lastAccessedDevice);
        // Not enough time for polling
        Cycle(serialClient, lastAccessedDevice);
    }
}

TEST_F(TPollTest, SingleDeviceSingleRegisterWithEventsAndPollingWithReadPeriod)
{
    // One register with events, one with read period and one without read period
    // 1. Events must be enabled
    // 2. The register is read once by normal request and excluded from polling
    // 3. Events read requests are sent every 50ms
    // 4. Register with read period must be polled during free time as much close to read period as possible
    // 5. Register without read period must be polled during free time

    Port->SetBaudRate(115200);
    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->RequestDelay = 10ms;
    auto device = MakeDevice(config);
    AddRegister(*device, 1, 0ms, TRegisterConfig::TSporadicMode::ONLY_EVENTS);
    AddRegister(*device, 2, 100ms);
    AddRegister(*device, 3);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    // Enable events and read registers
    EnqueueEnableEvents(1, 1, 10ms);
    EnqueueReadHolding(1, 2, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    // It is already time to read events
    EnqueueReadEvents(4ms);
    Cycle(serialClient, lastAccessedDevice);

    // Read the last register
    EnqueueReadHolding(1, 3, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);
    EnqueueReadHolding(1, 3, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    // Not enough time before reading register with read period
    Cycle(serialClient, lastAccessedDevice);

    // Not enough time before events reading
    Cycle(serialClient, lastAccessedDevice);

    // It is already time to read events
    EnqueueReadEvents(4ms);
    Cycle(serialClient, lastAccessedDevice);

    // Read register with read period
    EnqueueReadHolding(1, 2, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    // Read the last register
    EnqueueReadHolding(1, 3, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    // Not enough time before reading register with read period
    Cycle(serialClient, lastAccessedDevice);

    // It is already time to read events
    EnqueueReadEvents(4ms);
    Cycle(serialClient, lastAccessedDevice);

    // Read the last register
    EnqueueReadHolding(1, 3, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);
    EnqueueReadHolding(1, 3, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    // Not enough time before events reading
    Cycle(serialClient, lastAccessedDevice);

    // It is already time to read events
    EnqueueReadEvents(4ms);
    Cycle(serialClient, lastAccessedDevice);

    // Read register with read period
    EnqueueReadHolding(1, 2, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);
}

TEST_F(TPollTest, SingleDeviceEventsAndBigReadTime)
{
    // One register with events, one with read period and one without read period
    // 1. Events must be enabled
    // 2. The register is read once by normal request and excluded from polling
    // 3. Events read requests are sent every 50ms
    // 4. Register without read period must be polled after some time of event reads because of time balancing

    Port->SetBaudRate(115200);
    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->RequestDelay = 100ms;
    auto device = MakeDevice(config);
    AddRegister(*device, 1, 0ms, TRegisterConfig::TSporadicMode::ONLY_EVENTS);
    AddRegister(*device, 2);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    // Enable events and read registers
    EnqueueEnableEvents(1, 1, 10ms);
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    for (size_t i = 0; i < 13; ++i) {
        EnqueueReadEvents(4ms);
        Cycle(serialClient, lastAccessedDevice);

        // Calculated read time is too big
        Cycle(serialClient, lastAccessedDevice);
    }

    EnqueueReadEvents(4ms);
    Cycle(serialClient, lastAccessedDevice);

    // Read register
    EnqueueReadHolding(1, 2, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    for (size_t i = 0; i < 2; ++i) {
        for (size_t j = 0; j < 2; ++j) {
            EnqueueReadEvents(4ms);
            Cycle(serialClient, lastAccessedDevice);

            // Calculated read time is too big
            Cycle(serialClient, lastAccessedDevice);
        }

        EnqueueReadEvents(4ms);
        Cycle(serialClient, lastAccessedDevice);

        // Read register
        EnqueueReadHolding(1, 2, 1, 10ms);
        Cycle(serialClient, lastAccessedDevice);
    }
}

TEST_F(TPollTest, SingleDeviceSingleRegisterWithBigReadTime)
{
    // One register without fixed read period but with big read time
    // Check what poller reads it as soon as possible

    Port->SetBaudRate(115200);
    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->RequestDelay = 100ms;
    auto device = MakeDevice(config);
    AddRegister(*device, 1);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    for (size_t i = 0; i < 10; ++i) {
        EnqueueReadHolding(1, 1, 1, 30ms);
        Cycle(serialClient, lastAccessedDevice);
    }
}

TEST_F(TPollTest, SingleDeviceSingleRegisterWithEventsAndErrors)
{
    // One register with events
    // 1. Events must be enabled
    // 2. Read once by normal request
    // 3. Events read requests are sent every 50ms
    // 4. Some read requests have errors

    Port->SetBaudRate(115200);
    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->RequestDelay = 10ms;
    auto device = MakeDevice(config);
    AddRegister(*device, 1, 0ms, TRegisterConfig::TSporadicMode::ONLY_EVENTS);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    // Read registers
    EnqueueEnableEvents(1, 1, 10ms);
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    // Only read events
    for (size_t i = 0; i < 3; ++i) {
        EnqueueReadEvents(10ms);
        Cycle(serialClient, lastAccessedDevice);
    }

    for (size_t i = 0; i < 3; ++i) {
        EnqueueReadEvents(30ms, 0, true, MAX_EVENT_RESPONSE_SIZE);
        EnqueueReadEvents(30ms, 0, true, MAX_EVENT_RESPONSE_SIZE);
        EnqueueReadEvents(30ms, 0, true, MAX_EVENT_RESPONSE_SIZE);
        EnqueueReadEvents(25ms, 0, true, 0x40);
        Cycle(serialClient, lastAccessedDevice);
    }
}

TEST_F(TPollTest, SingleDeviceEnableEventsError)
{
    // One register with events
    // 1. Events must be enabled
    // 2. Read once by normal request
    // 3. Events read requests are sent every 50ms
    // 4. Some read requests have errors

    Port->SetBaudRate(115200);
    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->RequestDelay = 10ms;
    auto device = MakeDevice(config);
    AddRegister(*device, 1, 0ms, TRegisterConfig::TSporadicMode::ONLY_EVENTS);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    // Read registers
    EnqueueEnableEvents(1, 1, 10ms, 0x01, true);
    Cycle(serialClient, lastAccessedDevice);

    // Events are disabled, poll register
    EnqueueEnableEvents(1, 1, 10ms, 0x00);
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    for (size_t i = 0; i < 10; ++i) {
        EnqueueReadHolding(1, 1, 1, 10ms);
        Cycle(serialClient, lastAccessedDevice);
    }
}

TEST_F(TPollTest, SemiSporadicRegister)
{
    // One register with events and polling and one without read period
    // 1. Events must be enabled
    // 2. The register is read once by normal request and NOT excluded from polling
    // 3. Events read requests are sent every 50ms
    // 4. Both registers must be polled during free time

    Port->SetBaudRate(115200);
    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->RequestDelay = 10ms;
    auto device = MakeDevice(config);
    AddRegister(*device, 1, 0ms, TRegisterConfig::TSporadicMode::EVENTS_AND_POLLING);
    AddRegister(*device, 2);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    // Enable events and read first register
    EnqueueEnableEvents(1, 1, 10ms);
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);
    EnqueueReadHolding(1, 2, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    for (size_t i = 0; i < 3; ++i) {
        EnqueueReadEvents(4ms);
        Cycle(serialClient, lastAccessedDevice);
        EnqueueReadHolding(1, 1, 1, 10ms);
        Cycle(serialClient, lastAccessedDevice);
        EnqueueReadHolding(1, 2, 1, 10ms);
        Cycle(serialClient, lastAccessedDevice);
        // Not enough time for polling
        Cycle(serialClient, lastAccessedDevice);
    }
}

TEST_F(TPollTest, ReconnectWithOnlyEvents)
{
    // One register with events
    // 1. Events must be enabled
    // 2. Read once by normal request
    // 3. Events read requests are sent every 50ms (8 times to not trigger every 500ms polling)
    // 4. Simulate restart event
    // 5. Reenable events
    // 6. Read once by normal request
    // 7. Events read requests are sent every 50ms (10 times)
    // 8. The register is read again by normal request according to "sporadic only" device polling

    Port->SetBaudRate(115200);
    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->RequestDelay = 10ms;
    auto device = MakeDevice(config);
    AddRegister(*device, 1, 0ms, TRegisterConfig::TSporadicMode::ONLY_EVENTS);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    // Read registers
    EnqueueEnableEvents(1, 1, 10ms);
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    // Only read events
    for (size_t i = 0; i < 8; ++i) {
        EnqueueReadEvents(4ms);
        Cycle(serialClient, lastAccessedDevice);
    }

    // Device reset event
    EnqueueResetEvent(4ms, 1);
    EnqueueReadEvents(4ms, 1, false, MAX_EVENT_RESPONSE_SIZE);
    Cycle(serialClient, lastAccessedDevice);

    // Additional cycle for reading event but without actual read
    Cycle(serialClient, lastAccessedDevice);

    // Read registers
    EnqueueEnableEvents(1, 1, 10ms);
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    // Only read events
    for (size_t i = 0; i < 10; ++i) {
        EnqueueReadEvents(4ms);
        Cycle(serialClient, lastAccessedDevice);
    }

    // Read registers
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);
}

TEST_F(TPollTest, OnlyEventsPollError)
{
    // One register with events
    // 1. Events must be enabled
    // 2. Read once by normal request
    // 3. Events read requests are sent every 50ms (10 times)
    // 4. Simulate read response timeout and check for device is now disconnected
    // 5. Reenable events
    // 6. Read once by normal request and check for device is reconnected

    Port->SetBaudRate(115200);

    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->RequestDelay = 10ms;
    config.CommonConfig->DeviceTimeout = std::chrono::milliseconds(0);
    config.CommonConfig->DeviceMaxFailCycles = 1;

    auto device = MakeDevice(config);
    AddRegister(*device, 1, 0ms, TRegisterConfig::TSporadicMode::ONLY_EVENTS);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    // Enable events and read register
    EnqueueEnableEvents(1, 1, 10ms);
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    // Only read events
    for (size_t i = 0; i < 10; ++i) {
        EnqueueReadEvents(4ms);
        Cycle(serialClient, lastAccessedDevice);
    }

    // Read register with no response
    EnqueueReadHoldingError(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);
    EXPECT_EQ(device->GetConnectionState(), TDeviceConnectionState::DISCONNECTED);

    // Additional cycle for reading event but without actual read
    Cycle(serialClient, lastAccessedDevice);

    // Reenable events and read register
    EnqueueEnableEvents(1, 1, 10ms);
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);
    EXPECT_EQ(device->GetConnectionState(), TDeviceConnectionState::CONNECTED);
}

TEST_F(TPollTest, DisconnectedPollDelay)
{
    // One register without events
    // 1. Read once
    // 2. Simulate read response timeout and check for device is now disconnected'
    // 3. Check for poll interval is increased
    // 4. Repeat steps 2 and 3 another 24 times
    // 5. Read once and check for device is reconnected

    Port->SetBaudRate(115200);

    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->RequestDelay = 10ms;
    config.CommonConfig->DeviceTimeout = std::chrono::milliseconds(0);
    config.CommonConfig->DeviceMaxFailCycles = 1;

    auto device = MakeDevice(config);
    AddRegister(*device, 1);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    // Read register
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    auto time = TimeMock.GetTime() + 20ms; // 20ms added by read cycle
    auto delay = milliseconds(0);
    for (int i = 0; i < 25; ++i) {
        // Read register with no response
        EnqueueReadHoldingError(1, 1, 1, 10ms);
        Cycle(serialClient, lastAccessedDevice);
        EXPECT_EQ(device->GetConnectionState(), TDeviceConnectionState::DISCONNECTED);
        // Check for poll interval is increased
        EXPECT_EQ(ceil<milliseconds>(TimeMock.GetTime().time_since_epoch()).count(),
                  ceil<milliseconds>(time.time_since_epoch()).count());
        if (delay < DISCONNECTED_POLL_DELAY_LIMIT) {
            delay += DISCONNECTED_POLL_DELAY_STEP;
        }
        time += delay;
    }

    // Read register
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);
    EXPECT_EQ(device->GetConnectionState(), TDeviceConnectionState::CONNECTED);
}

TEST_F(TPollTest, SuspendAndResume)
{
    // One register without events
    // 1. Continuous read must be enabled
    // 2. Poll register (3 times)
    // 3. Suspend poll
    // 4. Wait a "minute" (3 times)
    // 5. Resume poll "manually"
    // 6. Reenable continuous read
    // 7. Poll register (3 times)

    Port->SetBaudRate(115200);

    auto config = MakeDeviceConfig("device1", "1");
    config.EnableWbContinuousRead = true;

    auto device = MakeDevice(config);
    AddRegister(*device, 1);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    EnqueueEnableContinuousRead(1, 10ms);
    for (size_t i = 0; i < 3; ++i) {
        EnqueueReadHolding(1, 1, 1, 10ms);
        Cycle(serialClient, lastAccessedDevice);
    }

    serialClient.SuspendPoll(device, TimeMock.GetTime());

    for (size_t i = 0; i < 3; ++i) {
        Cycle(serialClient, lastAccessedDevice);
        TimeMock.AddTime(1min);
    }

    serialClient.ResumePoll(device);

    EnqueueEnableContinuousRead(1, 10ms);
    for (size_t i = 0; i < 10; ++i) {
        EnqueueReadHolding(1, 1, 1, 10ms);
        Cycle(serialClient, lastAccessedDevice);
    }
}

TEST_F(TPollTest, SuspendAndResumeByTimeout)
{
    // One register without events
    // 1. Continuous read must be enabled
    // 2. Poll register (3 times)
    // 3. Suspend poll
    // 4. Wait a "minute" (10 times)
    // 5. Poll must be resumed by timeout
    // 6. Reenable continuous read
    // 7. Poll register (3 times)

    Port->SetBaudRate(115200);

    auto config = MakeDeviceConfig("device1", "1");
    config.EnableWbContinuousRead = true;

    auto device = MakeDevice(config);
    AddRegister(*device, 1);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    EnqueueEnableContinuousRead(1, 10ms);
    for (size_t i = 0; i < 3; ++i) {
        EnqueueReadHolding(1, 1, 1, 10ms);
        Cycle(serialClient, lastAccessedDevice);
    }

    serialClient.SuspendPoll(device, TimeMock.GetTime());

    for (size_t i = 0; i < 10; ++i) {
        Cycle(serialClient, lastAccessedDevice);
        TimeMock.AddTime(1min);
    }

    EnqueueEnableContinuousRead(1, 10ms);
    for (size_t i = 0; i < 3; ++i) {
        EnqueueReadHolding(1, 1, 1, 10ms);
        Cycle(serialClient, lastAccessedDevice);
    }
}

TEST_F(TPollTest, SuspendAndResumeWithEvents)
{
    // One register with events
    // 1. Events must be enabled
    // 2. Read once by normal request
    // 3. Events read requests are sent every 50ms (3 times)
    // 4. Suspend poll
    // 4. Wait a "minute" (3 times)
    // 6. Resume poll "manually"
    // 7. Reenable events
    // 8. Read once by normal request
    // 9. Events read requests are sent every 50ms (10 times)

    Port->SetBaudRate(115200);

    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->RequestDelay = 10ms;

    auto device = MakeDevice(config);
    AddRegister(*device, 1, 0ms, TRegisterConfig::TSporadicMode::ONLY_EVENTS);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    EnqueueEnableEvents(1, 1, 10ms);
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    for (size_t i = 0; i < 3; ++i) {
        EnqueueReadEvents(4ms);
        Cycle(serialClient, lastAccessedDevice);
    }

    serialClient.SuspendPoll(device, TimeMock.GetTime());

    for (size_t i = 0; i < 3; ++i) {
        Cycle(serialClient, lastAccessedDevice);
        TimeMock.AddTime(1min);
    }

    serialClient.ResumePoll(device);

    EnqueueEnableEvents(1, 1, 10ms);
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    for (size_t i = 0; i < 3; ++i) {
        EnqueueReadEvents(4ms);
        Cycle(serialClient, lastAccessedDevice);
    }
}

TEST_F(TPollTest, EventsCapLimitsConsecutiveReadsFromSameDevice)
{
    // A device that always has events must not monopolize the event bus.
    // After MAX_CONSECUTIVE_EVENT_READS_PER_SLAVE (5) reads in a row from the same
    // device the master excludes it from arbitration by raising min_slave to
    // slaveId + 1. The device is polled again from the next reading session.

    Port->SetBaudRate(115200);
    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->RequestDelay = 10ms;
    auto device = MakeDevice(config);
    AddRegister(*device, 1, 0ms, TRegisterConfig::TSporadicMode::ONLY_EVENTS);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    // Enable events and read the register once
    EnqueueEnableEvents(1, 1, 10ms);
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    // A single event-reading session. Device 1 answers five times in a row.
    EnqueueEventsExchange(4ms, 0, 0, 0, HoldingEventResponse(0, 1, 0x1234), 1);
    EnqueueEventsExchange(4ms, 1, 1, 0, HoldingEventResponse(0, 1, 0x1234), 1);
    EnqueueEventsExchange(4ms, 1, 1, 0, HoldingEventResponse(0, 1, 0x1234), 1);
    EnqueueEventsExchange(4ms, 1, 1, 0, HoldingEventResponse(0, 1, 0x1234), 1);
    EnqueueEventsExchange(4ms, 1, 1, 0, HoldingEventResponse(0, 1, 0x1234), 1);
    // Cap reached: device 1 is excluded, min_slave becomes 2 (device 1 is still confirmed).
    // NO_EVENTS ends the session; the next session starts again from min_slave 0.
    EnqueueEventsExchange(4ms, 2, 1, 0, NoEventsResponse(), 0xFD);
    Cycle(serialClient, lastAccessedDevice);
}

TEST_F(TPollTest, EventsCapPersistsBetweenReadingSessions)
{
    // The read-cap must accumulate ACROSS reading sessions: at low baud rates a single
    // session (bounded by poll time) fits only a couple of reads, so a per-session counter
    // would never reach the cap. Here each session fits exactly two reads (60ms each vs
    // 100ms poll time); the cap (5) is reached only if the streak survives the session
    // boundaries. The fifth overall read (in the third session) triggers the skip.

    Port->SetBaudRate(115200);
    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->RequestDelay = 10ms;
    auto device = MakeDevice(config);
    AddRegister(*device, 1, 0ms, TRegisterConfig::TSporadicMode::ONLY_EVENTS);

    TSerialClientRegisterAndEventsReader serialClient({device}, 50ms, [this]() { return TimeMock.GetTime(); });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    // Enable events and read the register once
    EnqueueEnableEvents(1, 1, 10ms);
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    // Session 1: two reads from device 1 (session ends by timeout, streak = 2 is kept)
    EnqueueEventsExchange(60ms, 0, 0, 0, HoldingEventResponse(0, 1, 0x1234), 1);
    EnqueueEventsExchange(60ms, 1, 1, 0, HoldingEventResponse(0, 1, 0x1234), 1);
    Cycle(serialClient, lastAccessedDevice);

    // Session 2: two more reads, still min_slave 1 (streak carried over, now 4)
    EnqueueEventsExchange(60ms, 1, 1, 0, HoldingEventResponse(0, 1, 0x1234), 1);
    EnqueueEventsExchange(60ms, 1, 1, 0, HoldingEventResponse(0, 1, 0x1234), 1);
    Cycle(serialClient, lastAccessedDevice);

    // Session 3: the fifth read reaches the cap -> skip to min_slave 2 -> NO_EVENTS ends the session
    EnqueueEventsExchange(60ms, 1, 1, 0, HoldingEventResponse(0, 1, 0x1234), 1);
    EnqueueEventsExchange(60ms, 2, 1, 0, NoEventsResponse(), 0xFD);
    Cycle(serialClient, lastAccessedDevice);

    // Session 4: back to min_slave 0, the bus is now quiet
    EnqueueEventsExchange(60ms, 0, 0, 0, NoEventsResponse(), 0xFD);
    Cycle(serialClient, lastAccessedDevice);
}

TEST_F(TPollTest, TimeSync)
{
    // One register, time synchronization every 24 hours
    // 1. Local time must be written after the first read
    // 2. No new write during the interval
    // 3. Local time must be written again after the interval

    Port->SetBaudRate(115200);

    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->TimeSyncInterval = 24h;

    auto device = MakeDevice(config);
    AddRegister(*device, 1);

    TSerialClientRegisterAndEventsReader serialClient(
        {device},
        50ms,
        [this]() { return TimeMock.GetTime(); },
        [this]() { return SystemTime; });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    EnqueueReadHolding(1, 1, 1, 10ms);
    EnqueueWriteLocalTime(1, FAKE_LOCAL_TIME, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    SystemTime += 23h;
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    SystemTime += 2h;
    EnqueueReadHolding(1, 1, 1, 10ms);
    EnqueueWriteLocalTime(1, FAKE_LOCAL_TIME + duration_cast<seconds>(25h).count(), 10ms);
    Cycle(serialClient, lastAccessedDevice);
}

TEST_F(TPollTest, TimeSyncDisabled)
{
    // Zero interval disables time synchronization, only registers are read

    Port->SetBaudRate(115200);

    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->TimeSyncInterval = TimeSyncDisabled;

    auto device = MakeDevice(config);
    AddRegister(*device, 1);

    TSerialClientRegisterAndEventsReader serialClient(
        {device},
        50ms,
        [this]() { return TimeMock.GetTime(); },
        [this]() { return SystemTime; });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    for (size_t i = 0; i < 3; ++i) {
        EnqueueReadHolding(1, 1, 1, 10ms);
        Cycle(serialClient, lastAccessedDevice);
        SystemTime += 25h;
    }
}

TEST_F(TPollTest, TimeSyncTransientFailure)
{
    // One register, time synchronization every 24 hours
    // Device fails to write local time, but the register is supported,
    // so the attempt is repeated on the next poll

    Port->SetBaudRate(115200);

    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->TimeSyncInterval = 24h;

    auto device = MakeDevice(config);
    AddRegister(*device, 1);

    TSerialClientRegisterAndEventsReader serialClient(
        {device},
        50ms,
        [this]() { return TimeMock.GetTime(); },
        [this]() { return SystemTime; });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    EnqueueReadHolding(1, 1, 1, 10ms);
    EnqueueWriteLocalTime(1, FAKE_LOCAL_TIME, 10ms, SLAVE_DEVICE_FAILURE);
    Cycle(serialClient, lastAccessedDevice);

    EnqueueReadHolding(1, 1, 1, 10ms);
    EnqueueWriteLocalTime(1, FAKE_LOCAL_TIME, 10ms);
    Cycle(serialClient, lastAccessedDevice);
}

TEST_F(TPollTest, TimeSyncNotSupported)
{
    // One register, time synchronization every 24 hours
    // 1. Device replies with ILLEGAL_DATA_ADDRESS to the local time write on the first poll
    // 2. No new attempts while the device is connected, even after the interval
    // 3. The attempt is repeated after reconnect

    Port->SetBaudRate(115200);

    auto config = MakeDeviceConfig("device1", "1");
    config.CommonConfig->TimeSyncInterval = 24h;
    config.CommonConfig->DeviceTimeout = 0ms;
    config.CommonConfig->DeviceMaxFailCycles = 1;

    auto device = MakeDevice(config);
    AddRegister(*device, 1);

    TSerialClientRegisterAndEventsReader serialClient(
        {device},
        50ms,
        [this]() { return TimeMock.GetTime(); },
        [this]() { return SystemTime; });
    TSerialClientDeviceAccessHandler lastAccessedDevice(serialClient.GetEventsReader());

    EnqueueReadHolding(1, 1, 1, 10ms);
    EnqueueWriteLocalTime(1, FAKE_LOCAL_TIME, 10ms, Modbus::ILLEGAL_DATA_ADDRESS);
    Cycle(serialClient, lastAccessedDevice);
    EXPECT_EQ(device->GetConnectionState(), TDeviceConnectionState::CONNECTED);

    SystemTime += 25h;
    EnqueueReadHolding(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);

    EnqueueReadHoldingError(1, 1, 1, 10ms);
    Cycle(serialClient, lastAccessedDevice);
    EXPECT_EQ(device->GetConnectionState(), TDeviceConnectionState::DISCONNECTED);

    EnqueueReadHolding(1, 1, 1, 10ms);
    EnqueueWriteLocalTime(1, FAKE_LOCAL_TIME + duration_cast<seconds>(25h).count(), 10ms);
    Cycle(serialClient, lastAccessedDevice);
    EXPECT_EQ(device->GetConnectionState(), TDeviceConnectionState::CONNECTED);
}
