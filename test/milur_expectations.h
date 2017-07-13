#pragma once
#include "expector.h"

class TMilurExpectations: public virtual TExpectorProvider
{
public:
	void EnqueueMilurIgnoredPacketWorkaround();
	void EnqueueMilurSessionSetupResponse();
	void EnqueueMilur32SessionSetupResponse();
	void EnqueueMilurAccessLevel2SessionSetupResponse();

	void EnqueueMilurPhaseAVoltageResponse();
	void EnqueueMilurPhaseBVoltageResponse();
	void EnqueueMilurPhaseCVoltageResponse();

	void EnqueueMilurPhaseACurrentResponse();
	void EnqueueMilurPhaseBCurrentResponse();
	void EnqueueMilurPhaseCCurrentResponse();

	void EnqueueMilurPhaseAActivePowerResponse();
	void EnqueueMilurPhaseBActivePowerResponse();
	void EnqueueMilurPhaseCActivePowerResponse();
	void EnqueueMilurTotalActivePowerResponse();

	void EnqueueMilurPhaseAReactivePowerResponse();
	void EnqueueMilurPhaseBReactivePowerResponse();
	void EnqueueMilurPhaseCReactivePowerResponse();
	void EnqueueMilurTotalReactivePowerResponse();

	void EnqueueMilurTotalConsumptionResponse();
	void EnqueueMilurTotalReactiveEnergyResponse();

	void EnqueueMilurFrequencyResponse();

	void EnqueueMilur32TotalConsumptionResponse();
	void EnqueueMilurNoSessionResponse();
	void EnqueueMilurExceptionResponse();
};
