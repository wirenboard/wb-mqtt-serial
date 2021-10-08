#include "fake_serial_port.h"
#include "modbus_io_expectations.h"
#include "modbus_common.h"

using namespace std;

class TModbusIOIntegrationTest: public TSerialDeviceIntegrationTest, public TModbusIOExpectations
{
protected:
    void SetUp();
    void TearDown();
    const char* ConfigPath() const override
    {
        return "configs/config-modbus-io-test.json";
    }
    std::string GetTemplatePath() const override
    {
        return "device-templates";
    }

    void ExpectPollQueries();

private:
    bool FirstPoll = true;
};

void TModbusIOIntegrationTest::SetUp()
{
    SelectModbusType(MODBUS_RTU);
    TSerialDeviceIntegrationTest::SetUp();
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
        EnqueueSetupSectionWriteResponse(true);
    }
    EnqueueCoilReadResponse(true);

    if (FirstPoll) {
        EnqueueSetupSectionWriteResponse(false);
        FirstPoll = false;
    }
    EnqueueCoilReadResponse(false);
}

TEST_F(TModbusIOIntegrationTest, Poll)
{
    ExpectPollQueries();
    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();

    ExpectPollQueries();
    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();
}

TEST_F(TModbusIOIntegrationTest, Write)
{
    ExpectPollQueries();
    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();

    PublishWaitOnValue("/devices/modbus-io-1-1/controls/Coil 0/on", "1");
    PublishWaitOnValue("/devices/modbus-io-1-2/controls/Coil 0/on", "0");

    EnqueueCoilWriteResponse(true);
    EnqueueCoilWriteResponse(false);

    ExpectPollQueries();
    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();
}

TEST_F(TModbusIOIntegrationTest, SetupErrors)
{
    EnqueueSetupSectionWriteResponse(true, true);
    EnqueueSetupSectionWriteResponse(false, true);
    EnqueueCoilReadResponse(false);

    Note() << "LoopOnce() [first start, one is disconnected]";
    SerialDriver->LoopOnce();

    EnqueueSetupSectionWriteResponse(true);
    EnqueueCoilReadResponse(true);
    EnqueueCoilReadResponse(false);

    Note() << "LoopOnce() [all are ok]";
    SerialDriver->LoopOnce();
}