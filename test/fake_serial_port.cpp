#include "fake_serial_port.h"

#include <wblib/backend.h>
#include <wblib/driver.h>
#include <wblib/driver_args.h>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <stdexcept>

#include "config_schema_generator.h"
#include "fake_serial_device.h"
#include "log.h"

using namespace WBMQTT;
using namespace WBMQTT::Testing;
using namespace std::literals;

namespace
{
    const auto DB_PATH = "/tmp/wb-mqtt-serial-test.db";
}

TFakeSerialPort::TFakeSerialPort(TLoggedFixture& fixture, const std::string& portName, bool emptyDescription)
    : Fixture(fixture),
      AllowOpen(true),
      IsPortOpen(false),
      DisconnectType(NoDisconnect),
      ReqPos(0),
      RespPos(0),
      DumpPos(0),
      BaudRate(9600),
      PortName(portName),
      EmptyDescription(emptyDescription)
{}

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
    if (!AllowOpen) {
        throw TSerialDeviceException("Port open error simulation");
    }
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

TLoggedFixture& TFakeSerialPort::GetFixture()
{
    return Fixture;
}

bool TFakeSerialPort::IsOpen() const
{
    return IsPortOpen;
}

void TFakeSerialPort::WriteBytes(const uint8_t* buf, int count)
{
    switch (DisconnectType) {
        case NoDisconnect:
            break;
        case SilentReadAndWriteFailure:
            return;
        case BadFileDescriptorOnWriteAndRead: {
            Fixture.Emit() << "write error EBADF";
            throw TSerialDeviceErrnoException("write error ", EBADF);
        }
    };

    SkipFrameBoundary();
    DumpWhatWasRead();
    if (!PendingFuncs.empty()) {
        Fixture.Emit() << PendingFuncs.front() << "()";
        PendingFuncs.pop_front();
    }
    ::Debug.Log() << "Port write " << Testing::HexDump(std::vector<uint8_t>(buf, buf + count));
    Fixture.Emit() << ">> " << std::vector<uint8_t>(buf, buf + count);

    if (Req.size() - ReqPos < size_t(count) + 1) {
        throw std::runtime_error(std::string("Request: ") + HexDump(std::vector<uint8_t>(buf, buf + count)) +
                                 " is longer than expected");
    }

    auto start = Req.begin() + ReqPos;
    if (Req[ReqPos + count] != FRAME_BOUNDARY) {
        throw std::runtime_error(std::string("Request mismatch: ") + HexDump(std::vector<uint8_t>(buf, buf + count)) +
                                 ", expected: " + HexDump(std::vector<uint8_t>(start, start + count)) +
                                 ", frame boundary " + std::to_string(Req[ReqPos + count]));
    }

    const uint8_t* p = buf;
    for (auto it = start; p < buf + count; ++p, ++it) {
        if (*it != int(*p)) {
            throw std::runtime_error(std::string("Request mismatch: ") +
                                     HexDump(std::vector<uint8_t>(buf, buf + count)) +
                                     ", expected: " + HexDump(std::vector<uint8_t>(start, start + count)));
        }
    }

    if (Req[ReqPos + count] != FRAME_BOUNDARY)
        throw std::runtime_error("Unexpectedly short request");

    ReqPos += count + 1;
}

uint8_t TFakeSerialPort::ReadByte(const std::chrono::microseconds& /*timeout*/)
{
    switch (DisconnectType) {
        case NoDisconnect:
            break;
        case SilentReadAndWriteFailure:
            return 0xFF;
        case BadFileDescriptorOnWriteAndRead: {
            Fixture.Emit() << "read error EBADF";
            throw TSerialDeviceErrnoException("read error ", EBADF);
        }
    };

    CheckPortOpen();

    while (RespPos < Resp.size() && Resp[RespPos] == FRAME_BOUNDARY)
        RespPos++;

    if (RespPos == Resp.size())
        throw std::runtime_error("response buffer underflow");

    return Resp[RespPos++];
}

TReadFrameResult TFakeSerialPort::ReadFrame(uint8_t* buf,
                                            size_t count,
                                            const std::chrono::microseconds& responseTimeout,
                                            const std::chrono::microseconds& frameTimeout,
                                            TFrameCompletePred frame_complete)
{
    TReadFrameResult res;

    switch (DisconnectType) {
        case NoDisconnect:
            break;
        case SilentReadAndWriteFailure:
            return res;
        case BadFileDescriptorOnWriteAndRead: {
            Fixture.Emit() << "read frame error EBADF";
            throw TSerialDeviceErrnoException("read frame error ", EBADF);
        }
    };

    if (ExpectedFrameTimeout.count() >= 0 && frameTimeout != ExpectedFrameTimeout) {
        DumpWhatWasRead();
        throw std::runtime_error("TFakeSerialPort::ReadFrame: bad timeout: " + std::to_string(frameTimeout.count()) +
                                 " instead of " + std::to_string(ExpectedFrameTimeout.count()));
    }
    uint8_t* p = buf;
    while (res.Count < count) {
        if (RespPos == Resp.size())
            break;
        int b = Resp[RespPos++];
        if (b == FRAME_BOUNDARY)
            break;
        *p++ = (uint8_t)b;
        ++res.Count;
        if (frame_complete && frame_complete(buf, res.Count)) {
            break;
        }
    }
    DumpWhatWasRead();
    if ((res.Count == 0) && (count != 0)) {
        throw TResponseTimeoutException();
    }
    return res;
}

void TFakeSerialPort::SkipNoise()
{
    CheckPortOpen();
    if (RespPos != 0 && Resp[RespPos - 1] != FRAME_BOUNDARY) {
        while (RespPos < Resp.size() && Resp[RespPos] != FRAME_BOUNDARY) {
            RespPos++;
        }
    }
    SkipFrameBoundary();
    Fixture.Emit() << "SkipNoise()";
    DumpWhatWasRead();
}

void TFakeSerialPort::SleepSinceLastInteraction(const std::chrono::microseconds& us)
{
    if (us > std::chrono::microseconds::zero()) {
        SkipFrameBoundary();
        Fixture.Emit() << "Sleep(" << us.count() << ")";
    }
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

void TFakeSerialPort::SimulateDisconnect(TFakeSerialPort::TDisconnectType simulate)
{
    DisconnectType = simulate;
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

void TFakeSerialPort::SetAllowOpen(bool allowOpen)
{
    AllowOpen = allowOpen;
}

std::chrono::microseconds TFakeSerialPort::GetSendTimeBytes(double bytesNumber) const
{
    auto us = std::ceil((1000000.0 * 11 * bytesNumber) / double(BaudRate));
    return std::chrono::microseconds(static_cast<std::chrono::microseconds::rep>(us));
}

std::string TFakeSerialPort::GetDescription(bool verbose) const
{
    return EmptyDescription ? "" : PortName;
}

void TFakeSerialPort::SetBaudRate(size_t value)
{
    BaudRate = value;
}

void TSerialDeviceTest::SetUp()
{
    RegisterProtocols(DeviceFactory);
    TFakeSerialDevice::Register(DeviceFactory);
    SerialPort = PFakeSerialPort(new TFakeSerialPort(*this, "<TSerialDeviceTest>"));
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

WBMQTT::TMap<std::string, std::shared_ptr<TTemplateMap>> TSerialDeviceIntegrationTest::Templates;
Json::Value TSerialDeviceIntegrationTest::CommonDeviceSchema;
Json::Value TSerialDeviceIntegrationTest::CommonConfigTemplatesSchema;

std::string TSerialDeviceIntegrationTest::GetTemplatePath() const
{
    return std::string();
}

void TSerialDeviceIntegrationTest::SetUpTestCase()
{
    if (CommonDeviceSchema.empty()) {
        CommonDeviceSchema = WBMQTT::JSON::Parse(GetDataFilePath("../wb-mqtt-serial-confed-common.schema.json"));
    }
    if (CommonConfigTemplatesSchema.empty()) {
        CommonConfigTemplatesSchema =
            LoadConfigTemplatesSchema(GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"),
                                      CommonDeviceSchema);
    }
}

void TSerialDeviceIntegrationTest::SetUp()
{
    SetMode(E_Unordered);
    TSerialDeviceTest::SetUp();
    PortMakerCalled = false;

    std::string path(GetTemplatePath());

    auto it = Templates.find(path);
    if (it == Templates.end()) {
        if (path.empty()) {
            it = Templates.emplace(path, std::make_shared<TTemplateMap>()).first;
        } else {
            auto templateMap = std::make_shared<TTemplateMap>(CommonConfigTemplatesSchema);
            templateMap->AddTemplatesDir(GetDataFilePath(path));
            it = Templates.emplace(path, templateMap).first;
        }
    }

    auto portsSchema = WBMQTT::JSON::Parse(GetDataFilePath("../wb-mqtt-serial-ports.schema.json"));
    TProtocolConfedSchemasMap protocolSchemas(GetDataFilePath("../protocols"), CommonDeviceSchema);

    Config = LoadConfig(GetDataFilePath(ConfigPath()),
                        DeviceFactory,
                        CommonDeviceSchema,
                        *it->second,
                        rpcConfig,
                        portsSchema,
                        protocolSchemas,
                        [=](const Json::Value&, PRPCConfig config) { return std::make_pair(SerialPort, false); });

    std::filesystem::remove(DB_PATH);
    MqttBroker = NewFakeMqttBroker(*this);
    MqttClient = MqttBroker->MakeClient("em-test");
    auto backend = NewDriverBackend(MqttClient);
    Driver = NewDriver(TDriverArgs{}
                           .SetId("em-test")
                           .SetBackend(backend)
                           .SetIsTesting(true)
                           .SetReownUnknownDevices(true)
                           .SetUseStorage(true)
                           .SetStoragePath(DB_PATH));

    Driver->StartLoop();

    SerialDriver = std::make_shared<TMQTTSerialDriver>(Driver, Config);
}

void TSerialDeviceIntegrationTest::TearDown()
{
    SerialDriver->ClearDevices();
    Driver->StopLoop();
    TSerialDeviceTest::TearDown();
    std::filesystem::remove(DB_PATH);
}

void TSerialDeviceIntegrationTest::Publish(const std::string& topic,
                                           const std::string& payload,
                                           uint8_t qos,
                                           bool retain)
{
    MqttBroker->Publish("em-test-other", {TMqttMessage{topic, payload, qos, retain}});
}

void TSerialDeviceIntegrationTest::PublishWaitOnValue(const std::string& topic,
                                                      const std::string& payload,
                                                      uint8_t qos,
                                                      bool retain)
{
    auto done = std::make_shared<WBMQTT::TPromise<void>>();
    Driver->On<WBMQTT::TControlOnValueEvent>([=](const WBMQTT::TControlOnValueEvent& event) {
        if (!done->IsFulfilled()) {
            done->Complete();
        }
    });
    Publish(topic, payload, qos, retain);
    done->GetFuture().Sync();
}
