#pragma once

#include <functional>
#include <signal.h>

namespace SignalHandling
{
    void Subscribe(int signal);
    void OnSignal(int signal, std::function<void()> callback);
    void Start();
    void Stop();
    void Wait();
}
