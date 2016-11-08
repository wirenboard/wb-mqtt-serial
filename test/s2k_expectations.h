#pragma once
#include "expector.h"

class TS2KDeviceExpectations: public virtual TExpectorProvider
{
protected:
    void EnqueueSetRelayOffResponse();
    void EnqueueSetRelayOnResponse();
    void EnqueueSetRelay2On();
    void EnqueueSetRelay3On2();
};
