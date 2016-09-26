#pragma once
#include "expector.h"

class TMercury200Expectations: public virtual TExpectorProvider
{
public:
    void EnqueueMercury200EnergyResponse();
    void EnqueueMercury200ParamResponse();
    void EnqueueMercury200BatteryVoltageResponse();
};
