#include <stdexcept>
#include "fake_serial_port.h"
#include "em_expectations.h"
#include "../modbus_config.h"
#include "../modbus_observer.h"

class TEMIntegrationTest: public TSerialProtocolIntegrationTest, public TEMProtocolExpectations
{
protected:
    const char* ConfigPath() const { return "../config-em-test.json"; }
};

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
