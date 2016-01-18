#pragma once
#include "expector.h"

class TUnielProtocolExpectations: public virtual TExpectorProvider
{
protected:
    void EnqueueVoltageQueryResponse();
    void EnqueueRelayOffQueryResponse();
    void EnqueueRelayOnQueryResponse();
    void EnqueueThreshold0QueryResponse();
    void EnqueueBrightnessQueryResponse();
    void EnqueueSetRelayOnResponse();
    void EnqueueSetRelayOffResponse();
    void EnqueueSetLowThreshold0Response();
    void EnqueueSetBrightnessResponse();
};
