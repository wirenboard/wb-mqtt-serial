#pragma once
#include "testlog.h"
#include "fake_serial_port.h"
#include "serial_connector.h"

class TEMProtocolTestBase: public TLoggedFixture
{
protected:
    void SetUp();
    void TearDown();
    void EnqueueMilurSessionSetupResponse();
    void EnqueueMilurAccessLevel2SessionSetupResponse();
    void EnqueueMilurPhaseCVoltageResponse();
    void EnqueueMilurTotalConsumptionResponse();
    void EnqueueMilurNoSessionResponse();
    void EnqueueMilurExceptionResponse();
    void EnqueueMercury230SessionSetupResponse();
    void EnqueueMercury230AccessLevel2SessionSetupResponse();
    void EnqueueMercury230EnergyResponse1();
    void EnqueueMercury230EnergyResponse2();
    void EnqueueMercury230U1Response();
    void EnqueueMercury230I1Response();
    void EnqueueMercury230U2Response();
    void EnqueueMercury230NoSessionResponse();
    void EnqueueMercury230InternalMeterErrorResponse();

    PFakeSerialPort SerialPort;
    PModbusContext Context;
};
