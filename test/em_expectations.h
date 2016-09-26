#pragma once
#include "expector.h"

class TEMDeviceExpectations: public virtual TExpectorProvider
{
public:
    void EnqueueMilurIgnoredPacketWorkaround();
    void EnqueueMilurSessionSetupResponse();
    void EnqueueMilurAccessLevel2SessionSetupResponse();
    void EnqueueMilurPhaseCVoltageResponse();
    void EnqueueMilurTotalConsumptionResponse();
    void EnqueueMilurNoSessionResponse();
    void EnqueueMilurExceptionResponse();
    void EnqueueMilurFrequencyResponse();
    void EnqueueMercury230SessionSetupResponse();
    void EnqueueMercury230AccessLevel2SessionSetupResponse();
    void EnqueueMercury230EnergyResponse1();
    void EnqueueMercury230EnergyResponse2();
    void EnqueueMercury230U1Response();
    void EnqueueMercury230I1Response();
    void EnqueueMercury230U2Response();
    void EnqueueMercury230PResponse();
    void EnqueueMercury230NoSessionResponse();
    void EnqueueMercury230InternalMeterErrorResponse();
    void EnqueueMilur32TotalConsumptionResponse();
    void EnqueueMilur32SessionSetupResponse();
};
