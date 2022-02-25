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
                  const TPortOpenCloseLogic::TSettings& openCloseSettings,
                  Metrics::TMetrics& metrics);
    TSerialClient(const TSerialClient& client) = delete;
    TSerialClient& operator=(const TSerialClient&) = delete;
    ~TSerialClient();

    void AddRegister(PRegister reg);
    void Cycle(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    void SetTextValue(PRegister reg, const std::string& value);
    void SetReadCallback(const TCallback& callback);
    void SetErrorCallback(const TCallback& callback);
    void NotifyFlushNeeded();
    void ClearDevices();

private:
    void Activate();
    void Connect();
    void PrepareRegisterRanges();
    void DoFlush();
    void WaitForPollAndFlush(std::chrono::steady_clock::time_point waitUntil);
    void MaybeFlushAvoidingPollStarvationButDontWait();
    void SetReadError(PRegister reg);
    PRegisterHandler GetHandler(PRegister) const;
    void SetRegistersAvailability(PSerialDevice dev, TRegisterAvailability availability);
    void ClosedPortCycle(std::chrono::steady_clock::time_point now);
    void OpenPortCycle(std::chrono::steady_clock::time_point now);
    void UpdateFlushNeeded();
    void ProcessPolledRegister(PRegister reg);
    void ScheduleNextPoll(PRegister reg, std::chrono::steady_clock::time_point pollStartTime);

    PPort Port;
    std::list<PRegister> RegList;
    std::vector<PSerialDevice> Devices;
    std::unordered_map<PRegister, PRegisterHandler> Handlers;

    bool Active;
    TCallback ReadCallback;
    TCallback ErrorCallback;
    PSerialDevice LastAccessedDevice;
    PBinarySemaphore FlushNeeded;
    TScheduler<PRegister, TRegisterComparePredicate> Scheduler;

    TPortOpenCloseLogic OpenCloseLogic;
    TLoggerWithTimeout ConnectLogger;
    Metrics::TMetrics& Metrics;
};

typedef std::shared_ptr<TSerialClient> PSerialClient;
