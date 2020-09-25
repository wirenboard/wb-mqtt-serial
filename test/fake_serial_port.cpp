#include "fake_serial_port.h"

#include <wblib/driver.h>
#include <wblib/backend.h>
#include <wblib/driver_args.h>

#include <cassert>
#include <algorithm>
#include <stdexcept>

using namespace WBMQTT;
using namespace WBMQTT::Testing;

TFakeSerialPort::TFakeSerialPort(TLoggedFixture& fixture)
    : Fixture(fixture), IsPortOpen(false), DoSimulateDisconnect(false), ReqPos(0), RespPos(0), DumpPos(0) {}

void TFakeSerialPort::SetExpectedFrameTimeout(const std::chrono::microseconds& timeout)
{
    ExpectedFrameTimeout = timeout;
}

void TFakeSerialPort::CheckPortOpen() const
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

TLoggedFixture & TFakeSerialPort::GetFixture()
{
    return Fixture;
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

    if (Req.size() - ReqPos < size_t(count) + 1) {
        throw std::runtime_error(std::string("Request: ") +
                HexDump(std::vector<uint8_t>(buf, buf + count)) +
                " is longer than expected");
    }

    auto start = Req.begin() + ReqPos;
    if (Req[ReqPos + count] != FRAME_BOUNDARY) {
        throw std::runtime_error(std::string("Request mismatch: ") +
                HexDump(std::vector<uint8_t>(buf, buf + count)) +
                ", expected: " +
                HexDump(std::vector<uint8_t>(start, start + count)) +
                ", frame boundary " + std::to_string(Req[ReqPos + count])
            );
    }

    const uint8_t* p = buf;
    for (auto it = start; p < buf + count; ++p, ++it) {
        if (*it != int(*p)) {
            throw std::runtime_error(std::string("Request mismatch: ") +
                    HexDump(std::vector<uint8_t>(buf, buf + count)) +
                    ", expected: " +
                    HexDump(std::vector<uint8_t>(start, start + count))
                );
        }
    }


    if (Req[ReqPos + count] != FRAME_BOUNDARY)
        throw std::runtime_error("Unexpectedly short request");

    ReqPos += count + 1;
}

uint8_t TFakeSerialPort::ReadByte(const std::chrono::microseconds& /*timeout*/)
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

int TFakeSerialPort::ReadFrame(uint8_t* buf, 
                               int count,
                               const std::chrono::microseconds& responseTimeout,
                               const std::chrono::microseconds& frameTimeout,
                               TFrameCompletePred frame_complete)
{
    if (DoSimulateDisconnect) {
        return 0;
    }
    if (ExpectedFrameTimeout.count() >= 0 && frameTimeout != ExpectedFrameTimeout)
        throw std::runtime_error("TFakeSerialPort::ReadFrame: bad timeout: " +
                                 std::to_string(frameTimeout.count()) + " instead of " +
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

void TFakeSerialPort::SleepSinceLastInteraction(const std::chrono::microseconds& us)
{
    if (us > std::chrono::microseconds::zero()) {
        SkipFrameBoundary();
        DumpWhatWasRead();
        Fixture.Emit() << "Sleep(" << us.count() << ")";
    }
}

bool TFakeSerialPort::Wait(const PBinarySemaphore & semaphore, const TTimePoint & until)
{
    if (semaphore->TryWait())
        return true;
    if (until < Time)
        throw std::runtime_error("TFakeSerialPort::Wait(): going back in time");
    Time = until;
    return false;
}

TTimePoint TFakeSerialPort::CurrentTime() const
{
    return Time;
}

void TFakeSerialPort::CycleEnd(bool ok)
{
    Fixture.Emit() << (ok ? "Port cycle OK" : "Port cycle FAIL");
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

bool TFakeSerialPort::GetDoSimulateDisconnect() const
{
    return DoSimulateDisconnect;
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

std::chrono::milliseconds TFakeSerialPort::GetSendTime(double bytesNumber)
{
    // 9600 8-N-2
    auto ms = std::ceil((1000.0*11*bytesNumber)/9600.0);
    return std::chrono::milliseconds(static_cast<std::chrono::milliseconds::rep>(ms));
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

WBMQTT::TMap<std::string, TTemplateMap> TSerialDeviceIntegrationTest::Templates;

std::string TSerialDeviceIntegrationTest::GetTemplatePath() const 
{
    return std::string();
}

void TSerialDeviceIntegrationTest::SetUp()
{
    SetMode(E_Unordered);
    TSerialDeviceTest::SetUp();
    PortMakerCalled = false;

    Json::Value configSchema = LoadConfigSchema(GetDataFilePath("../wb-mqtt-serial.schema.json"));

    std::string path(GetTemplatePath());

    auto it = Templates.find(path);
    if (it == Templates.end()) {
        if (path.empty()) {
            it = Templates.emplace(path, TTemplateMap()).first;
        } else {
            it = Templates.emplace(path, 
                                   TTemplateMap(GetDataFilePath(path),
                                                LoadConfigTemplatesSchema(GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"), 
                                                                          configSchema))).first;
        } 
    }

    Config = LoadConfig(GetDataFilePath(ConfigPath()), 
                         TSerialDeviceFactory::GetRegisterTypes,
                         configSchema,
                         it->second);

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
