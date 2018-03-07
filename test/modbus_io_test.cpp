#include "fake_serial_port.h"
#include "modbus_io_expectations.h"
#include "modbus_common.h"

using namespace std;


class TModbusIOIntegrationTest: public TSerialDeviceIntegrationTest, public TModbusIOExpectations
{
protected:
    void SetUp();
    void TearDown();
    const char* ConfigPath() const override { return "configs/config-modbus-io-test.json"; }
    const char* GetTemplatePath() const override { return "device-templates/";}

    void ExpectPollQueries();

private:
    bool FirstPoll = true;
};

void TModbusIOIntegrationTest::SetUp()
{
    SelectModbusType(MODBUS_RTU);
    TSerialDeviceIntegrationTest::SetUp();
    Observer->SetUp();
    ASSERT_TRUE(!!SerialPort);
}

void TModbusIOIntegrationTest::TearDown()
{
    SerialPort->Close();
    TSerialDeviceIntegrationTest::TearDown();
}

void TModbusIOIntegrationTest::ExpectPollQueries()
{
    if (FirstPoll) {
        EnqueueSetupSectionWriteResponse();
        FirstPoll = false;
    }
    EnqueueCoilReadResponse();
}


TEST_F(TModbusIOIntegrationTest, Poll)
{
    ExpectPollQueries();
    Observer->WriteInitValues();
    Note() << "LoopOnce()";
    Observer->LoopOnce();

    ExpectPollQueries();
    Note() << "LoopOnce()";
    Observer->LoopOnce();
}

TEST_F(TModbusIOIntegrationTest, Write)
{
    ExpectPollQueries();
    Observer->WriteInitValues();
    Note() << "LoopOnce()";
    Observer->LoopOnce();

    MQTTClient->Publish(nullptr, "/devices/modbus-io-1-1/controls/Coil 0/on", "1");
    MQTTClient->Publish(nullptr, "/devices/modbus-io-1-2/controls/Coil 0/on", "0");

    EnqueueCoilWriteResponse();

    ExpectPollQueries();
    Note() << "LoopOnce()";
    Observer->LoopOnce();
}
