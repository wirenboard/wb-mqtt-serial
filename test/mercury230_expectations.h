#pragma once

#include "expector.h"

class TMercury230Expectations: public virtual TExpectorProvider
{
public:
	void EnqueueMercury230SessionSetupResponse();
	void EnqueueMercury230AccessLevel2SessionSetupResponse();
	void EnqueueMercury230EnergyResponse1();
	void EnqueueMercury230EnergyResponse2();

	void EnqueueMercury230U1Response();
	void EnqueueMercury230U2Response();
	void EnqueueMercury230U3Response();

	void EnqueueMercury230I1Response();
	void EnqueueMercury230I2Response();
	void EnqueueMercury230I3Response();

	void EnqueueMercury230FrequencyResponse();

	void EnqueueMercury230KU1Response();
	void EnqueueMercury230KU2Response();
	void EnqueueMercury230KU3Response();

	void EnqueueMercury230PResponse();
	void EnqueueMercury230P1Response();
	void EnqueueMercury230P2Response();
	void EnqueueMercury230P3Response();

	void EnqueueMercury230PFResponse();
	void EnqueueMercury230PF1Response();
	void EnqueueMercury230PF2Response();
	void EnqueueMercury230PF3Response();

	void EnqueueMercury230QResponse();
	void EnqueueMercury230Q1Response();
	void EnqueueMercury230Q2Response();
	void EnqueueMercury230Q3Response();

	void EnqueueMercury230TempResponse();
	void EnqueueMercury230PerPhaseEnergyResponse();
	void EnqueueMercury230NoSessionResponse();
	void EnqueueMercury230InternalMeterErrorResponse();
};
