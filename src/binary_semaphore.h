#pragma once

#include <chrono>
#include <mutex>
#include <memory>
#include <condition_variable>

class TBinarySemaphore {
public:
    template<class Clock, class Duration>
    bool Wait(const std::chrono::time_point<Clock, Duration>& until)
    {
        std::unique_lock<std::mutex> lock(Mutex);
        bool r = Cond.wait_until(lock, until, [this](){ return _Signaled; });
        _Signaled = false;
        return r;
    }
    bool TryWait()
    {
        std::unique_lock<std::mutex> lock(Mutex);
        bool r = _Signaled;
        _Signaled = false;
        return r;
    }
    void Signal()
    {
        std::unique_lock<std::mutex> lock(Mutex);
        _Signaled = true;
        Cond.notify_all();
    }
private:
    bool _Signaled = false;
    std::mutex Mutex;
    std::condition_variable Cond;
};

typedef std::shared_ptr<TBinarySemaphore> PBinarySemaphore;
