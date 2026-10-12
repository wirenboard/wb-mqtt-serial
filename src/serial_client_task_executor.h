#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "serial_client.h"

class TSerialClientTaskExecutor: public util::TNonCopyable
{
public:
    TSerialClientTaskExecutor(PFeaturePort port);
    ~TSerialClientTaskExecutor();

    //! A task added after RequestStop is cancelled at once
    void AddTask(PSerialClientTask task);

    //! Cancels the queued tasks
    void RequestStop();

    //! Returns false if the executor still runs a task which is not cancelled at the deadline
    bool WaitStopped(std::chrono::steady_clock::time_point deadline);

    void Resume();

    PFeaturePort GetPort() const;

    bool IsIdle() const;

private:
    PFeaturePort Port;

    mutable std::mutex Mutex;
    std::condition_variable TasksCv;
    std::deque<PSerialClientTask> Tasks;
    std::condition_variable IdleCv;
    bool StopRequested;

    std::thread Thread;
    std::atomic<bool> Running;
    bool Idle;
};

typedef std::shared_ptr<TSerialClientTaskExecutor> PSerialClientTaskExecutor;
