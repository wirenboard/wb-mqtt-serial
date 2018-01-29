#include "fake_serial_port.h"
#include "utils.h"

#include <wblib/driver.h>
#include <wblib/backend.h>
#include <wblib/driver_args.h>

#include <cassert>
#include <algorithm>
#include <stdexcept>

using namespace WBMQTT;

TFakeSerialPort::TFakeSerialPort(TLoggedFixture& fixture)
    : Fixture(fixture), IsPortOpen(false), DoSimulateDisconnect(false), ReqPos(0), RespPos(0), DumpPos(0) {}

void TFakeSerialPort::SetDebug(bool debug)
{
    Fixture.Emit() << "SetDebug(" << debug << ")";
}

bool TFakeSerialPort::Debug() const
{
    return false;
}

void TFakeSerialPort::SetExpectedFrameTimeout(const std::chrono::microseconds& timeout)
{
    ExpectedFrameTimeout = timeout;
}

void TFakeSerialPort::CheckPortOpen()
{
    if (!IsPortOpen)
        throw TSerialDeviceException("port not open");
}

void TFakeSerialPort::Open()
{
    if (IsPortOpen)
        throw TSerialDeviceException("port already open");
    Fixture.Emit() << "Open()";
    IsPortOpen = true;
}

void TFakeSerialPort::Close()
{
    CheckPortOpen();
    SkipFrameBoundary();
    DumpWhatWasRead();
    Fixture.Emit() << "Close()";
    IsPortOpen = false;
    if (ReqPos < Req.size())
        throw std::runtime_error("not all of expected requests received");
    if (RespPos < Resp.size())
        throw std::runtime_error("not all bytes in the response consumed");
}

bool TFakeSerialPort::IsOpen() const
{
    return IsPortOpen;
}

void TFakeSerialPort::WriteBytes(const uint8_t* buf, int count) {
    if (DoSimulateDisconnect) {
        return;
    }

    SkipFrameBoundary();
    DumpWhatWasRead();
    if (!PendingFuncs.empty()) {
        Fixture.Emit() << PendingFuncs.front() << "()";
        PendingFuncs.pop_front();
    }
    Fixture.Emit() << ">> " << std::vector<uint8_t>(buf, buf + count);
    auto start = Req.begin() + ReqPos;
    try {
        if (Req.size() - ReqPos < size_t(count) + 1 || Req[ReqPos + count] != FRAME_BOUNDARY)
            throw std::runtime_error("Request mismatch");

        const uint8_t* p = buf;
        for (auto it = start; p < buf + count; ++p, ++it) {
            if (*it != int(*p))
                throw std::runtime_error(std::string("Request mismatch: ") +
                        HexDump(std::vector<uint8_t>(buf, buf + count)) +
                        ", expected: " +
                        HexDump(std::vector<uint8_t>(start, start + count))
                    );
        }


        if (Req[ReqPos + count] != FRAME_BOUNDARY)
            throw std::runtime_error("Unexpectedly short request");

        ReqPos += count + 1;
    } catch (const std::runtime_error& e) {
        auto stop = std::find(start, Req.end(), FRAME_BOUNDARY);
        if (start != stop)
            Fixture.Emit() << "*> " << std::vector<uint8_t>(start, stop);
        else
            Fixture.Emit() << "*> req empty";
        throw;
    }
}

uint8_t TFakeSerialPort::ReadByte()
{
    if (DoSimulateDisconnect) {
        return 0xff;
    }
    CheckPortOpen();

    while (RespPos < Resp.size() && Resp[RespPos] == FRAME_BOUNDARY)
        RespPos++;

    if (RespPos == Resp.size())
        throw std::runtime_error("response buffer underflow");

    return Resp[RespPos++];
}

int TFakeSerialPort::ReadFrame(uint8_t* buf, int count,
                               const std::chrono::microseconds& timeout,
                               TFrameCompletePred frame_complete)
{
    if (DoSimulateDisconnect) {
        return 0;
    }
    if (ExpectedFrameTimeout.count() >= 0 && timeout != ExpectedFrameTimeout)
        throw std::runtime_error("TFakeSerialPort::ReadFrame: bad timeout: " +
                                 std::to_string(timeout.count()) + " instead of " +
                                 std::to_string(ExpectedFrameTimeout.count()));
    int nread = 0;
    uint8_t* p = buf;
    for (; nread < count; ++nread) {
        if (RespPos == Resp.size())
            break;
        int b = Resp[RespPos++];
        // TBD: after frame_ready arg is added,
        // make sure the frame becomes 'ready' exactly on the boundary.
        // Note though that frame_ready will be optional.
        if (b == FRAME_BOUNDARY)
            break;
        *p++ = (uint8_t)b;
    }
    if (frame_complete && !frame_complete(buf, nread))
        throw std::runtime_error("incomplete frame read");
    return nread;
}

void TFakeSerialPort::SkipNoise()
{
    CheckPortOpen();
    SkipFrameBoundary();
    DumpWhatWasRead();
    Fixture.Emit() << "SkipNoise()";
}

void TFakeSerialPort::Sleep(const std::chrono::microseconds& us)
{
    SkipFrameBoundary();
    DumpWhatWasRead();
    Fixture.Emit() << "Sleep(" << us.count() << ")";
}

TAbstractSerialPort::TTimePoint TFakeSerialPort::CurrentTime() const
{
    return Time;
}

bool TFakeSerialPort::Wait(PBinarySemaphore semaphore, const TTimePoint& until)
{
    if (semaphore->TryWait())
        return true;
    if (until < Time)
        throw std::runtime_error("TFakeSerialPort::Wait(): going back in time");
    Time = until;
    return false;
}

void TFakeSerialPort::DumpWhatWasRead()
{
    assert(DumpPos <= RespPos);
    if (DumpPos == RespPos)
        return;

    std::vector<uint8_t> slice;
    for (; DumpPos < RespPos; ++DumpPos) {
        if (Resp[DumpPos] == FRAME_BOUNDARY) {
            if (slice.size() > 0)
                Fixture.Emit() << "<< " << slice;
            slice.clear();
        } else
            slice.push_back(Resp[DumpPos]);
    }

    if (slice.size() > 0)
        Fixture.Emit() << "<< " << slice;

    DumpPos = RespPos;
}

void TFakeSerialPort::Elapse(const std::chrono::milliseconds& ms)
{
    Time += ms;
}

void TFakeSerialPort::SimulateDisconnect(bool simulate)
{
    DoSimulateDisconnect = simulate;
}

void TFakeSerialPort::Expect(const std::vector<int>& request, const std::vector<int>& response, const char* func)
{
    if (func)
        PendingFuncs.push_back(func);
    Req.insert(Req.end(), request.begin(), request.end());
    Req.push_back(FRAME_BOUNDARY);
    Resp.insert(Resp.end(), response.begin(), response.end());
    Resp.push_back(FRAME_BOUNDARY);
}

void TFakeSerialPort::SkipFrameBoundary()
{
    if (RespPos < Resp.size() && Resp[RespPos] == FRAME_BOUNDARY)
        RespPos++;
}

void TSerialDeviceTest::SetUp()
{
    SerialPort = PFakeSerialPort(new TFakeSerialPort(*this));
}

PExpector TSerialDeviceTest::Expector() const
{
    return SerialPort;
}

void TSerialDeviceTest::TearDown()
{
    SerialPort.reset();
    TLoggedFixture::TearDown();
}

void TSerialDeviceIntegrationTest::SetUp()
{
    SetMode(E_Unordered);
    TSerialDeviceTest::SetUp();
    PortMakerCalled = false;

    PTemplateMap templateMap = std::make_shared<TTemplateMap>();
    if (GetTemplatePath()) {
        TConfigTemplateParser templateParser(GetDataFilePath(GetTemplatePath()), false);
        templateMap = templateParser.Parse();
    }
    TConfigParser parser(GetDataFilePath(ConfigPath()), false, TSerialDeviceFactory::GetRegisterTypes, templateMap);


    Config = parser.Parse();
    MqttBroker = NewFakeMqttBroker(*this);
    MqttClient = MqttBroker->MakeClient("em-test");
    auto backend = NewDriverBackend(MqttClient);
    Driver = NewDriver(TDriverArgs{}
        .SetId("em-test")
        .SetBackend(backend)
        .SetIsTesting(true)
        .SetReownUnknownDevices(true)
        .SetUseStorage(true)
        .SetStoragePath("/tmp/wb-mqtt-serial-test.db")
    );

    Driver->StartLoop();

    SerialDriver = std::make_shared<TMQTTSerialDriver>(Driver, Config, SerialPort);
}

void TSerialDeviceIntegrationTest::TearDown()
{
    SerialDriver->ClearDevices();
    Driver->StopLoop();
    TSerialDeviceTest::TearDown();
}

void TSerialDeviceIntegrationTest::Publish(const std::string & topic, const std::string & payload, uint8_t qos, bool retain)
{
    MqttBroker->Publish("em-test-other", {TMqttMessage{topic, payload, qos, retain}});
}

void TSerialDeviceIntegrationTest::PublishWaitOnValue(const std::string & topic, const std::string & payload, uint8_t qos, bool retain)
{
    auto done = std::make_shared<WBMQTT::TPromise<void>>();
    Driver->On<WBMQTT::TControlOnValueEvent>([=](const WBMQTT::TControlOnValueEvent & event) {
        if (!done->IsFulfilled()) {
            done->Complete();
        }
    });
    Publish(topic, payload, qos, retain);
    done->GetFuture().Sync();
}
