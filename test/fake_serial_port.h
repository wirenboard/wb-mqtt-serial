#pragma once

#include <vector>
#include <memory>
#include <deque>

#include <wblib/testing/fake_mqtt.h>
#include <wblib/testing/testlog.h>
#include <wblib/map.h>

#include "expector.h"
#include "serial_device.h"
#include "serial_driver.h"

using WBMQTT::Testing::TLoggedFixture;

class TFakeSerialPort: public TPort, public TExpector {
public:
    TFakeSerialPort(WBMQTT::Testing::TLoggedFixture& fixture);

    void SetExpectedFrameTimeout(const std::chrono::microseconds& timeout);
    void CheckPortOpen() const;
    void Open();
    void Close();
    bool IsOpen() const;
    void WriteBytes(const uint8_t* buf, int count);
    uint8_t ReadByte(const std::chrono::microseconds& timeout);
    size_t ReadFrame(uint8_t* buf, size_t count,
                     const std::chrono::microseconds& responseTimeout = std::chrono::microseconds(-1),
                     const std::chrono::microseconds& frameTimeout = std::chrono::microseconds(-1),
                     TFrameCompletePred frame_complete = 0);
    void SkipNoise();

    void SleepSinceLastInteraction(const std::chrono::microseconds& us) override;
    bool Wait(const PBinarySemaphore & semaphore, const TTimePoint & until) override;
    TTimePoint CurrentTime() const override;
    void CycleEnd(bool ok) override;

    std::chrono::milliseconds GetSendTime(double bytesNumber) override;

    void Expect(const std::vector<int>& request, const std::vector<int>& response, const char* func = 0);
    void DumpWhatWasRead();
    void Elapse(const std::chrono::milliseconds& ms);
    void SimulateDisconnect(bool simulate);
    bool GetDoSimulateDisconnect() const;
    WBMQTT::Testing::TLoggedFixture& GetFixture();

    std::string GetDescription() const override;

    void SetAllowOpen(bool allowOpen);

private:
    void SkipFrameBoundary();
    const int FRAME_BOUNDARY = -1;

    WBMQTT::Testing::TLoggedFixture& Fixture;
    bool AllowOpen;
    bool IsPortOpen;
    bool DoSimulateDisconnect;
    std::deque<const char*> PendingFuncs;
    std::vector<int> Req;
    std::vector<int> Resp;
    size_t ReqPos, RespPos, DumpPos;
    std::chrono::microseconds ExpectedFrameTimeout = std::chrono::microseconds(-1);
    TPollPlan::TTimePoint Time = TPollPlan::TTimePoint(std::chrono::milliseconds(0));
};

typedef std::shared_ptr<TFakeSerialPort> PFakeSerialPort;

class TSerialDeviceTest: public WBMQTT::Testing::TLoggedFixture, public virtual TExpectorProvider {
protected:
    void SetUp();
    void TearDown();
    PExpector Expector() const;

    PFakeSerialPort SerialPort;
    TSerialDeviceFactory DeviceFactory;
};

class TSerialDeviceIntegrationTest: public virtual TSerialDeviceTest {
protected:
    void SetUp();
    void TearDown();
    void Publish(const std::string & topic, const std::string & payload, uint8_t qos = 0, bool retain = true);
    void PublishWaitOnValue(const std::string & topic, const std::string & payload, uint8_t qos = 0, bool retain = true);
    virtual const char* ConfigPath() const = 0;
    virtual std::string GetTemplatePath() const;

    WBMQTT::Testing::PFakeMqttBroker MqttBroker;
    WBMQTT::Testing::PFakeMqttClient MqttClient;
    WBMQTT::PDeviceDriver            Driver;

    PMQTTSerialDriver SerialDriver;
    PHandlerConfig Config;
    bool PortMakerCalled;

    static WBMQTT::TMap<std::string, TTemplateMap> Templates; // Key - path to folder, value - loaded templates map
};
