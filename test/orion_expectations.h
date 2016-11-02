#pragma once
#include "expector.h"

class TOrionDeviceExpectations: public virtual TExpectorProvider
{
protected:
    void EnqueueSetRelayOffResponse();
    void EnqueueSetRelayOnResponse();
    void EnqueueSetRelay2On();
    void EnqueueSetRelay3On2();
};
