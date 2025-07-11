
#include "fake_serial_port.h"
#include "log.h"
#include "modbus_expectations_base.h"
#include "serial_driver.h"

using namespace std::chrono_literals;
using namespace std::chrono;

namespace
{

    const uint8_t MAX_EVENT_RESPONSE_SIZE = 0xF8;

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
        TimeMock.Reset();
        Port = std::make_shared<TFakeSerialPortWithTime>(*this, TimeMock);
        TModbusDevice::Register(DeviceFactory);
        Port->Open();
    }

    void TearDown() override
    {
        Port->Close();
        TLoggedFixture::TearDown();
    }

    void Cycle(TSerialClientRegisterAndEventsReader& serialClient, TSerialClientDeviceAccessHandler& lastAccessedDevice)
    {
        Emit() << ceil<microseconds>(TimeMock.GetTime().time_since_epoch()).count() << ": Cycle";
        serialClient.OpenPortCycle(
            *Port,
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
    TTimeMock TimeMock;
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
