#pragma once

#include "rpc_request.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

class TQueueMessage
{
public:
    virtual ~TQueueMessage(){};
};

typedef std::shared_ptr<TQueueMessage> PQueueMessage;

class TSetValueQueueMessage: public TQueueMessage
{};

typedef std::shared_ptr<TSetValueQueueMessage> PSetValueQueueMessage;

class TRPCQueueMessage: public TQueueMessage
{
public:
    PPort Port;
    PRPCRequest Request;
    std::condition_variable Condition;
    std::shared_ptr<std::atomic<bool>> Done;
    std::shared_ptr<std::atomic<bool>> Cancelled;
    std::shared_ptr<std::vector<uint8_t>> Response;
    std::shared_ptr<TRPCRequestResultCode> ResultCode;
};

typedef std::shared_ptr<TRPCQueueMessage> PRPCQueueMessage;

class TSerialClientQueue
{
public:
    template<class Clock, class Duration> bool WaitMessage(const std::chrono::time_point<Clock, Duration>& until)
    {
        std::unique_lock<std::mutex> lock(Mutex);

        return Cond.wait_until(lock, until, [this]() { return !queue.empty(); });
    }

    void PushMessage(PQueueMessage message)
    {
        std::unique_lock<std::mutex> lock(Mutex);
        queue.push(message);
        Cond.notify_all();
    }

    PQueueMessage PopMessage()
    {
        std::unique_lock<std::mutex> lock(Mutex);

        if (queue.empty()) {
            return nullptr;
        }

        PQueueMessage message = queue.front();
        queue.pop();
        return message;
    }

private:
    std::mutex Mutex;
    std::condition_variable Cond;
    std::queue<PQueueMessage> queue;
};

typedef std::shared_ptr<TSerialClientQueue> PSerialClientQueue;
