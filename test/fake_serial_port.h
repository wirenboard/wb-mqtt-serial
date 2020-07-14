#pragma once

#include <vector>
#include <memory>
#include <deque>

#include <wblib/testing/fake_mqtt.h>
#include <wblib/testing/testlog.h>

#include "expector.h"
#include "serial_device.h"
#include "serial_driver.h"

using WBMQTT::Testing::TLoggedFixture;

class TFakeSerialPort: public TAbstractSerialPort, public TExpector {
public:
    TFakeSerialPort(TLoggedFixture& fixture);
    void SetExpectedFrameTimeout(const std::chrono::microseconds& timeout);
    void CheckPortOpen();
    void Open();
    void Close();
    bool IsOpen() const;
    void WriteBytes(const uint8_t* buf, int count);
    uint8_t ReadByte();
    int ReadFrame(uint8_t* buf, int count,
                  const std::chrono::microseconds& timeout = std::chrono::microseconds(-1),
                  TFrameCompletePred frame_complete = 0);
    void SkipNoise();
    void Sleep(const std::chrono::microseconds& us);
    TTimePoint CurrentTime() const;
    bool Wait(PBinarySemaphore semaphore, const TTimePoint& until);

    void Expect(const std::vector<int>& request, const std::vector<int>& response, const char* func = 0);
    void DumpWhatWasRead();
    void Elapse(const std::chrono::milliseconds& ms);
    void SimulateDisconnect(bool simulate);

private:
    void SkipFrameBoundary();
    const int FRAME_BOUNDARY = -1;

    TLoggedFixture& Fixture;
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

class TSerialDeviceTest: public TLoggedFixture, public virtual TExpectorProvider {
protected:
    void SetUp();
    void TearDown();
    PExpector Expector() const;

    PFakeSerialPort SerialPort;
};

class TSerialDeviceIntegrationTest: public virtual TSerialDeviceTest {
protected:
    void SetUp();
    void TearDown();
    void Publish(const std::string & topic, const std::string & payload, uint8_t qos = 0, bool retain = true);
    void PublishWaitOnValue(const std::string & topic, const std::string & payload, uint8_t qos = 0, bool retain = true);
    virtual const char* ConfigPath() const = 0;
    virtual const char* GetTemplatePath() const { return nullptr;};

    WBMQTT::Testing::PFakeMqttBroker MqttBroker;
    WBMQTT::Testing::PFakeMqttClient MqttClient;
    WBMQTT::PDeviceDriver            Driver;

    PMQTTSerialDriver SerialDriver;
    PHandlerConfig Config;
    bool PortMakerCalled;
};
