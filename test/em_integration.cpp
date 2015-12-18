#include <stdexcept>
#include "em_test.h"
#include "fake_mqtt.h"
#include "../modbus_config.h"
#include "../modbus_observer.h"

class TEMIntegrationTest: public TEMProtocolTestBase
{
protected:
    void SetUp();
    void TearDown();

    PMQTTModbusObserver Observer;
    bool PortMakerCalled;
};

void TEMIntegrationTest::SetUp()
{
    TEMProtocolTestBase::SetUp();
    PortMakerCalled = false;
    TSerialConnector::SetGlobalPortMaker([this](const TSerialPortSettings&) {
            if (PortMakerCalled)
                throw std::runtime_error("serial port reinit?");
            PortMakerCalled = true;
            return SerialPort;
        });
    TConfigParser parser(GetDataFilePath("../config-em-test.json"), false);
    PHandlerConfig Config = parser.Parse();
    PFakeMQTTClient mqttClient(new TFakeMQTTClient("em-test", *this));
    Observer = PMQTTModbusObserver(new TMQTTModbusObserver(mqttClient, Config));
}

void TEMIntegrationTest::TearDown()
{
    TSerialConnector::SetGlobalPortMaker(0);
    Observer.reset();
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
