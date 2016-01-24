#pragma once

#include <vector>
#include <memory>
#include <deque>

#include "expector.h"
#include "testlog.h"
#include "fake_mqtt.h"
#include "../wb-homa-modbus/serial_protocol.h"
#include "../serial_observer.h"

class TFakeSerialPort: public TAbstractSerialPort, public TExpector {
public:
    TFakeSerialPort(TLoggedFixture& fixture);
    void SetDebug(bool debug);
    void CheckPortOpen();
    void Open();
    void Close();
    bool IsOpen() const;
    void WriteBytes(const uint8_t* buf, int count);
    uint8_t ReadByte();
    int ReadFrame(uint8_t* buf, int count, int timeout);
    void SkipNoise();
    void USleep(int usec);
    PLibModbusContext LibModbusContext() const;

    void Expect(const std::vector<int>& request, const std::vector<int>& response, const char* func = 0);
    void DumpWhatWasRead();

private:
    void SkipFrameBoundary();
    const int FRAME_BOUNDARY = -1;

    TLoggedFixture& Fixture;
    bool IsPortOpen;
    std::deque<const char*> PendingFuncs;
    std::vector<int> Req;
    std::vector<int> Resp;
    size_t ReqPos, RespPos, DumpPos;
};

typedef std::shared_ptr<TFakeSerialPort> PFakeSerialPort;

class TSerialProtocolTest: public TLoggedFixture, public virtual TExpectorProvider {
protected:
    void SetUp();
    void TearDown();
    PExpector Expector() const;

    PFakeSerialPort SerialPort;
};

class TSerialProtocolIntegrationTest: public virtual TSerialProtocolTest {
protected:
    void SetUp();
    void TearDown();
    virtual const char* ConfigPath() const = 0;

    PFakeMQTTClient MQTTClient;
    PMQTTSerialObserver Observer;
    bool PortMakerCalled;
};
