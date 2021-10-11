#include "fake_serial_port.h"
#include "mercury230_expectations.h"
#include "milur_expectations.h"
#include "serial_config.h"
#include "serial_driver.h"
#include <stdexcept>

class TEMIntegrationTest: public TSerialDeviceIntegrationTest, public TMilurExpectations, public TMercury230Expectations
{
protected:
    void SetUp();
    void TearDown();
    const char* ConfigPath() const
    {
        return "configs/config-em-test.json";
    }
    void ExpectQueries(bool firstPoll);
};

void TEMIntegrationTest::SetUp()
{
    TSerialDeviceIntegrationTest::SetUp();
    SerialPort->SetExpectedFrameTimeout(std::chrono::milliseconds(150));
    ASSERT_TRUE(!!SerialPort);
}

void TEMIntegrationTest::TearDown()
{
    SerialPort->Close();
    TSerialDeviceIntegrationTest::TearDown();
}

void TEMIntegrationTest::ExpectQueries(bool firstPoll)
{
    EnqueueMilurIgnoredPacketWorkaround();

    if (firstPoll)
        EnqueueMilurSessionSetupResponse();
    EnqueueMilurPhaseCVoltageResponse();
    EnqueueMilurPhaseCCurrentResponse();
    EnqueueMilurTotalConsumptionResponse();

    if (firstPoll)
        EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230EnergyResponse1();
    EnqueueMercury230U1Response();
    EnqueueMercury230U2Response();
    EnqueueMercury230I1Response();
    EnqueueMercury230PResponse();
    EnqueueMercury230P1Response();
    EnqueueMercury230Q1Response();
    EnqueueMercury230Q2Response();
    EnqueueMercury230TempResponse();
    EnqueueMercury230PerPhaseEnergyResponse();
}

TEST_F(TEMIntegrationTest, Poll)
{
    ExpectQueries(true);

    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();
}

// TBD: test errors
