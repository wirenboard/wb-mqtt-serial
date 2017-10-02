#include "global.h"
#include "binary_semaphore.h"

#include <iostream>
#include <unistd.h>

using namespace std;


namespace Global
{
    bool DebugEnabled;

    void SetDebug(bool debug)
    {
        DebugEnabled = debug;
    }

    bool Debug()
    {
        return DebugEnabled;
    }

    void Sleep(const chrono::microseconds & us)
    {
        usleep(us.count());
    }

    TTimePoint CurrentTime()
    {
        return chrono::steady_clock::now();
    }

    bool Wait(const PBinarySemaphore & semaphore, const TTimePoint & until)
    {
        if (DebugEnabled) {
            cerr << chrono::duration_cast<chrono::milliseconds>(
                chrono::steady_clock::now().time_since_epoch()).count() <<
                ": Wait until " <<
                chrono::duration_cast<chrono::milliseconds>(until.time_since_epoch()).count() <<
                endl;
            }
                
        return semaphore->Wait(until);
    }
}
