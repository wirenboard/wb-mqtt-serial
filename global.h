#pragma once

#include "definitions.h"

// global driver state and utilities
namespace Global
{
    void SetDebug(bool debug);
    bool Debug();
    void Sleep(const std::chrono::microseconds & us);
    TTimePoint CurrentTime();
    bool Wait(const PBinarySemaphore & semaphore, const TTimePoint & until);
}
