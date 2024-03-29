#include "fake_serial_port.h"
#include "mercury230_expectations.h"
#include "milur_expectations.h"
#include "serial_config.h"
#include "serial_driver.h"
#include <stdexcept>

class TEMIntegrationTest: public TSerialDeviceIntegrationTest, public TMilurExpectations, public TMercury230Expectations
{
protected:
    void SetUp() override;
    void TearDown() override;
    const char* ConfigPath() const override
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

    EnqueueMilurIgnoredPacketWorkaround();
    if (firstPoll)
        EnqueueMilurSessionSetupResponse();
    EnqueueMilurPhaseCVoltageResponse();
    EnqueueMilurPhaseCCurrentResponse();
    EnqueueMilurTotalConsumptionResponse();
}

TEST_F(TEMIntegrationTest, Poll)
{
    ExpectQueries(true);

    Note() << "LoopOnce()";
    for (auto i = 0; i < 13; ++i) {
        SerialDriver->LoopOnce();
    }
}

// TBD: test errors
