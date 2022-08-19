#pragma once

#include <deque>
#include <memory>
#include <vector>

#include <wblib/map.h>
#include <wblib/testing/fake_mqtt.h>
#include <wblib/testing/testlog.h>

#include "expector.h"
#include "serial_device.h"
#include "serial_driver.h"

using WBMQTT::Testing::TLoggedFixture;

class TFakeSerialPort: public TPort, public TExpector
{
public:
    enum TDisconnectType
    {
        NoDisconnect,                   //! All operations are successful
        SilentReadAndWriteFailure,      //! Port can be successfully open, but all operations do nothing
        BadFileDescriptorOnWriteAndRead //! Port can be successfully open, but all operations fail with EBADF
    };

    TFakeSerialPort(WBMQTT::Testing::TLoggedFixture& fixture);

    void SetExpectedFrameTimeout(const std::chrono::microseconds& timeout);
    void CheckPortOpen() const;
    void Open();
    void Close();
    bool IsOpen() const;
    void WriteBytes(const uint8_t* buf, int count);
    uint8_t ReadByte(const std::chrono::microseconds& timeout);
    size_t ReadFrame(uint8_t* buf,
                     size_t count,
                     const std::chrono::microseconds& responseTimeout = std::chrono::microseconds(-1),
                     const std::chrono::microseconds& frameTimeout = std::chrono::microseconds(-1),
                     TFrameCompletePred frame_complete = 0);
    void SkipNoise();

    void SleepSinceLastInteraction(const std::chrono::microseconds& us) override;

    std::chrono::milliseconds GetSendTime(double bytesNumber) const override;

    void Expect(const std::vector<int>& request, const std::vector<int>& response, const char* func = 0);
    void DumpWhatWasRead();
    void SimulateDisconnect(TDisconnectType simulate);
    WBMQTT::Testing::TLoggedFixture& GetFixture();

    std::string GetDescription(bool verbose = true) const override;

    void SetAllowOpen(bool allowOpen);

private:
    void SkipFrameBoundary();
    const int FRAME_BOUNDARY = -1;

    WBMQTT::Testing::TLoggedFixture& Fixture;
    bool AllowOpen;
    bool IsPortOpen;
    TDisconnectType DisconnectType;
    std::deque<const char*> PendingFuncs;
    std::vector<int> Req;
    std::vector<int> Resp;
    size_t ReqPos, RespPos, DumpPos;
    std::chrono::microseconds ExpectedFrameTimeout = std::chrono::microseconds(-1);
};

typedef std::shared_ptr<TFakeSerialPort> PFakeSerialPort;

class TSerialDeviceTest: public WBMQTT::Testing::TLoggedFixture, public virtual TExpectorProvider
{
protected:
    void SetUp();
    void TearDown();
    PExpector Expector() const;

    PFakeSerialPort SerialPort;
    TSerialDeviceFactory DeviceFactory;
    PRPCConfig rpcConfig;
};

class TSerialDeviceIntegrationTest: public virtual TSerialDeviceTest
{
protected:
    void SetUp();
    void TearDown();
    void Publish(const std::string& topic, const std::string& payload, uint8_t qos = 0, bool retain = true);
    void PublishWaitOnValue(const std::string& topic, const std::string& payload, uint8_t qos = 0, bool retain = true);
    virtual const char* ConfigPath() const = 0;
    virtual std::string GetTemplatePath() const;

    WBMQTT::Testing::PFakeMqttBroker MqttBroker;
    WBMQTT::Testing::PFakeMqttClient MqttClient;
    WBMQTT::PDeviceDriver Driver;

    PMQTTSerialDriver SerialDriver;
    PHandlerConfig Config;
    bool PortMakerCalled;

    static WBMQTT::TMap<std::string, TTemplateMap> Templates; // Key - path to folder, value - loaded templates map
    static Json::Value CommonConfigSchema;
    static Json::Value CommonConfigTemplatesSchema;
    static void SetUpTestCase();
};
