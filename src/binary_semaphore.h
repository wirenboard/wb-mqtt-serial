#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

class TBinarySemaphoreSignal
{
public:
    bool value;
};

typedef std::shared_ptr<TBinarySemaphoreSignal> PBinarySemaphoreSignal;

class TBinarySemaphore
{
public:
    template<class Clock, class Duration> bool Wait(const std::chrono::time_point<Clock, Duration>& until)
    {
        std::unique_lock<std::mutex> lock(Mutex);

        return Cond.wait_until(lock, until, [this]() {
            return std::any_of(Signals.begin(), Signals.end(), [](PBinarySemaphoreSignal item) { return item->value; });
        });
    }

    bool GetSignalValue(PBinarySemaphoreSignal signal)
    {
        std::unique_lock<std::mutex> lock(Mutex);

        bool r = signal->value;
        signal->value = false;
        return r;
    }

    void Signal(PBinarySemaphoreSignal signal)
    {
        std::unique_lock<std::mutex> lock(Mutex);
        signal->value = true;
        Cond.notify_all();
    }

    void ResetAllSignals()
    {
        std::unique_lock<std::mutex> lock(Mutex);
        for (auto signal: Signals) {
            signal->value = false;
        }
    }

    PBinarySemaphoreSignal MakeSignal()
    {
        std::unique_lock<std::mutex> lock(Mutex);
        PBinarySemaphoreSignal new_signal = std::make_shared<TBinarySemaphoreSignal>();
        Signals.push_back(new_signal);
        new_signal->value = false;
        return new_signal;
    }

private:
    std::vector<PBinarySemaphoreSignal> Signals;
    std::mutex Mutex;
    std::condition_variable Cond;
};

typedef std::shared_ptr<TBinarySemaphore> PBinarySemaphore;
