#pragma once

#include <functional>
#include <list>
#include <memory>
#include <unordered_map>

#include "binary_semaphore.h"
#include "log.h"
#include "metrics.h"
#include "poll_plan.h"
#include "register_handler.h"
#include "serial_device.h"

struct TRegisterComparePredicate
{
    bool operator()(const PRegister& r1, const PRegister& r2) const;
};

class TSerialClient: public std::enable_shared_from_this<TSerialClient>
{
public:
    typedef std::function<void(PRegister reg)> TCallback;

    TSerialClient(const std::vector<PSerialDevice>& devices,
                  PPort port,
                  const TPortOpenCloseLogic::TSettings& openCloseSettings
#ifdef ENABLE_METRICS
                  ,
                  Metrics::TMetrics& metrics
#endif
    );
    TSerialClient(const TSerialClient& client) = delete;
    TSerialClient& operator=(const TSerialClient&) = delete;
    ~TSerialClient();

    void AddRegister(PRegister reg);
    void Cycle();
    void SetTextValue(PRegister reg, const std::string& value);
    void SetReadCallback(const TCallback& callback);
    void SetErrorCallback(const TCallback& callback);
    void ClearDevices();

private:
    void Activate();
    void Connect();
    void PrepareRegisterRanges();
    void DoFlush(const std::vector<PRegisterHandler>& commands);
    void WaitForPollAndFlush(std::chrono::steady_clock::time_point waitUntil);
    void SetReadError(PRegister reg);
    void SetRegistersAvailability(PSerialDevice dev, TRegisterAvailability availability);
    void ClosedPortCycle();
    void OpenPortCycle();
    void ProcessPolledRegister(PRegister reg);
    void ScheduleNextPoll(PRegister reg, std::chrono::steady_clock::time_point pollStartTime);
    bool PrepareToAccessDevice(PSerialDevice dev);
    void RetryCommand(PRegisterHandler command);

    PPort Port;
    std::list<PRegister> RegList;
    std::vector<PSerialDevice> Devices;

    bool Active;
    TCallback ReadCallback;
    TCallback ErrorCallback;
    PSerialDevice LastAccessedDevice;
    TScheduler<PRegister, TRegisterComparePredicate> Scheduler;

    std::mutex CommandsQueueMutex;
    std::condition_variable CommandsCV;
    std::vector<PRegisterHandler> Commands;

    TPortOpenCloseLogic OpenCloseLogic;
    TLoggerWithTimeout ConnectLogger;

#ifdef ENABLE_METRICS
    Metrics::TMetrics& Metrics;
#endif
};

typedef std::shared_ptr<TSerialClient> PSerialClient;
