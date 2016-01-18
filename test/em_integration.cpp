#include <stdexcept>
#include "em_test.h"
#include "../modbus_config.h"
#include "../modbus_observer.h"

class TEMIntegrationTest: public TSerialProtocolIntegrationTest, public TEMProtocolTestBase
{
protected:
    void SetUp();
    void TearDown();
    const char* ConfigPath() const { return "../config-em-test.json"; }
};

void TEMIntegrationTest::SetUp()
{
    TEMProtocolTestBase::SetUp();
    TSerialProtocolIntegrationTest::SetUp();
}

void TEMIntegrationTest::TearDown()
{
    TSerialProtocolIntegrationTest::TearDown();
    TEMProtocolTestBase::TearDown();
}

TEST_F(TEMIntegrationTest, Poll)
{
    Observer->SetUp();
    ASSERT_TRUE(!!SerialPort);

    EnqueueMilurSessionSetupResponse();
    EnqueueMilurPhaseCVoltageResponse();
    EnqueueMilurTotalConsumptionResponse();
    EnqueueMercury230SessionSetupResponse();
    EnqueueMercury230EnergyResponse1();
    EnqueueMercury230U1Response();
    EnqueueMercury230U2Response();
    EnqueueMercury230I1Response();

    Note() << "ModbusLoopOnce()";
    Observer->ModbusLoopOnce();
}

// TBD: test errors
