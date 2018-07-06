#pragma once

#include "expector.h"

class TS2KDeviceExpectations: public virtual TExpectorProvider
{
protected:
    void EnqueueSetRelayOffResponse();
    void EnqueueSetRelayOnResponse();
    void EnqueueSetRelay2On();
    void EnqueueSetRelay3On2();
    void EnqueueReadConfig1();
    void EnqueueReadConfig2();
    void EnqueueReadConfig3();
    void EnqueueReadConfig4();
    void EnqueueReadConfig5();
    void EnqueueReadConfig6();
    void EnqueueReadConfig7();
    void EnqueueReadConfig8();
};
