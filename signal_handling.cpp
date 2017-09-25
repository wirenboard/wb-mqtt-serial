#include "signal_handling.h"

#include <wbmqtt/utils.h>
#include <wbmqtt/log.h>

#include <thread>
#include <atomic>
#include <mutex>
#include <stack>
#include <map>

using namespace std;
using namespace WBMQTT;

unique_ptr<thread>                  SignalHandlingThread;
map<int, vector<function<void()>>>  Callbacks;
sigset_t                            Signals;
atomic_bool                         Init(false),
                                    Active(false),
                                    IsWaited(false);

namespace
{
    void SignalHandlingLoop()
    {
        siginfo_t signal;
        while (Active.load()) {
            Debug.Log() << "[signal handling] waiting for signals...";

            sigwaitinfo(&Signals, &signal);

            Debug.Log() << "[signal handling] received signal " << signal.si_signo;

            const auto & callbacks = Callbacks[signal.si_signo];

            for (auto it = callbacks.rbegin(); it != callbacks.rend(); ++it) {
                (*it)();
            }   
        }
    }
}

namespace SignalHandling
{
    void Subscribe(int signal)
    {
        if (!Init.load()) {
            sigemptyset(&Signals);
            Init.store(true);
        }
        sigaddset(&Signals, signal);
        sigprocmask(SIG_BLOCK, &Signals, NULL);
    }

    void OnSignal(int signal, function<void()> callback)
    {
        if (SignalHandlingThread) {
            throw runtime_error("Signal handling started - cannot add new handlers");
        }

        Callbacks[signal].push_back(move(callback));
    }

    void Start()
    {
        if (Active.load()) {
            throw runtime_error("Signal handling already started");
        }

        if (!Callbacks.empty()) {
            Active.store(true);
            SignalHandlingThread = MakeThread("signal handling", {SignalHandlingLoop});
        }
    }

    void Stop()
    {
        Active.store(false);
        Wait();
    }

    void Wait()
    {
        if (!IsWaited.load() && SignalHandlingThread && SignalHandlingThread->joinable()) {
            IsWaited.store(true);
            SignalHandlingThread->join();
            IsWaited.store(false);
        }
    }
}
