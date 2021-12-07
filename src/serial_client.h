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

class TRegisterReader
{
    PRegisterRange RegisterRange;
    std::chrono::steady_clock::time_point PollStart;
    std::list<PRegister> Regs;
    Metrics::TMetrics& Metrics;
    bool DeviceWasDisconnected = false;
    PSerialDevice LastAccessedDevice;
    bool DeviceIsConnected = false;

public:
    TRegisterReader(std::chrono::steady_clock::time_point pollStart,
                    PSerialDevice lastAccessedDevice,
                    Metrics::TMetrics& metrics);

    bool operator()(const PRegister& reg, std::chrono::milliseconds pollLimit);
    void Read();
    std::list<PRegister>& GetRegisters();
    bool GetDeviceWasDisconnected() const;
};

class TClosedPortRegisterReader
{
    std::list<PRegister> Regs;

public:
    bool operator()(const PRegister& reg, std::chrono::milliseconds pollLimit);
    std::list<PRegister>& GetRegisters();
};

class TSerialClient: public std::enable_shared_from_this<TSerialClient>
{
public:
    typedef std::function<void(PRegister reg, bool changed)> TReadCallback;
    typedef std::function<void(PRegister reg, TRegisterHandler::TErrorState errorState)> TErrorCallback;

    TSerialClient(const std::vector<PSerialDevice>& devices,
                  PPort port,
                  const TPortOpenCloseLogic::TSettings& openCloseSettings,
                  Metrics::TMetrics& metrics,
                  std::chrono::milliseconds lowPriorityPollInterval);
    TSerialClient(const TSerialClient& client) = delete;
    TSerialClient& operator=(const TSerialClient&) = delete;
    ~TSerialClient();

    void AddRegister(PRegister reg);
    void Cycle();
    void SetTextValue(PRegister reg, const std::string& value);
    std::string GetTextValue(PRegister reg) const;
    bool DidRead(PRegister reg) const;
    void SetReadCallback(const TReadCallback& callback);
    void SetErrorCallback(const TErrorCallback& callback);
    void NotifyFlushNeeded();
    void ClearDevices();

private:
    void Activate();
    void Connect();
    void PrepareRegisterRanges();
    void DoFlush();
    void WaitForPollAndFlush();
    void MaybeFlushAvoidingPollStarvationButDontWait();
    void SetReadError(PRegister reg);
    PRegisterHandler GetHandler(PRegister) const;
    void MaybeUpdateErrorState(PRegister reg, TRegisterHandler::TErrorState state);
    void SetRegistersAvailability(PSerialDevice dev, TRegister::TRegisterAvailability availability);
    void ClosedPortCycle();
    void OpenPortCycle();
    void UpdateFlushNeeded();
    void ProcessPolledRegister(PRegister reg);
    void ScheduleNextPoll(PRegister reg, bool isHighPriority, std::chrono::steady_clock::time_point now);

    PPort Port;
    std::list<PRegister> RegList;
    std::vector<PSerialDevice> Devices;
    std::unordered_map<PRegister, PRegisterHandler> Handlers;

    bool Active;
    TReadCallback ReadCallback;
    TErrorCallback ErrorCallback;
    PSerialDevice LastAccessedDevice;
    PBinarySemaphore FlushNeeded;
    TScheduler<PRegister, TRegisterComparePredicate> Scheduler;

    TPortOpenCloseLogic OpenCloseLogic;
    TLoggerWithTimeout ConnectLogger;
    Metrics::TMetrics& Metrics;
    std::chrono::milliseconds LowPriorityPollInterval;
};

typedef std::shared_ptr<TSerialClient> PSerialClient;
