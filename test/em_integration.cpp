#include <stdexcept>
#include "fake_serial_port.h"
#include "em_expectations.h"
#include "serial_config.h"
#include "serial_observer.h"

class TEMIntegrationTest: public TSerialDeviceIntegrationTest, public TEMDeviceExpectations
{
protected:
    void SetUp();
    void TearDown();
    const char* ConfigPath() const { return "../config-em-test.json"; }
    void ExpectQueries(bool firstPoll);
};

void TEMIntegrationTest::SetUp()
{
    TSerialDeviceIntegrationTest::SetUp();
    Observer->SetUp();
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
        EnqueueMilurSessionSetupResponse();
    EnqueueMilurPhaseCVoltageResponse();
    EnqueueMilurTotalConsumptionResponse();

    if (firstPoll)
        EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230EnergyResponse1();
    EnqueueMercury230U1Response();
    EnqueueMercury230U2Response();
    EnqueueMercury230I1Response();
}

TEST_F(TEMIntegrationTest, Poll)
{
    ExpectQueries(true);

    Note() << "LoopOnce()";
    Observer->LoopOnce();
}

// NOTE: max unchanged interval tests concern the whole driver,
// not just EM case, but it's hard to test for modbus devices
// because pty_based_fake_serial has to be used there.

TEST_F(TEMIntegrationTest, MaxUnchangedInterval) {
    for (int i = 0; i < 6; ++i) {
        if (i == 2 || i == 5)
            SerialPort->Elapse(std::chrono::seconds(100)); // force 'unchanged' update
        ExpectQueries(i == 0);

        Note() << "LoopOnce()";
        Observer->LoopOnce();
    }
}

TEST_F(TEMIntegrationTest, ZeroMaxUnchangedInterval) {
    // Patching config after the driver is initialized is unpretty,
    // but adding separate fixture for this test case due to config
    // changes would be even uglier.
    Config->MaxUnchangedInterval = 0;
    for (auto portConfig: Config->PortConfigs)
        portConfig->MaxUnchangedInterval = 0;

    for (int i = 0; i < 3; ++i) {
        ExpectQueries(i == 0);
        Note() << "LoopOnce()";
        Observer->LoopOnce();
    }
}

// TBD: test errors
