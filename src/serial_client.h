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
    std::chrono::milliseconds MaxPollTime;
    PSerialDevice Device;

public:
    TRegisterReader(std::chrono::milliseconds maxPollTime);
    bool operator()(const PRegister& reg, std::chrono::milliseconds pollLimit);
    PRegisterRange GetRegisterRange() const;
    void Reset(std::chrono::milliseconds maxPollTime);
};

class TClosedPortRegisterReader
{
    std::list<PRegister> Regs;

public:
    bool operator()(const PRegister& reg, std::chrono::milliseconds pollLimit);
    std::list<PRegister>& GetRegisters();
};

enum ERPCState
{
    RPC_IDLE,
    RPC_WRITE,
    RPC_READ,
    RPC_ERROR
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
    void Cycle();
    void SetTextValue(PRegister reg, const std::string& value);
    void SetReadCallback(const TCallback& callback);
    void SetErrorCallback(const TCallback& callback);
    void NotifyFlushNeeded();
    void ClearDevices();

    void RPCWrite(const std::vector<uint8_t>& buf,
                  size_t responseSize,
                  std::chrono::milliseconds respTimeout,
                  std::chrono::milliseconds frameTimeout);
    bool RPCRead(std::vector<uint8_t>& buf, size_t& actualSize, bool& error);
    void GetPortInfo(Json::Value& info);

private:
    void Activate();
    void Connect();
    void PrepareRegisterRanges();
    void DoFlush();
    void WaitForPollAndFlush();
    void MaybeFlushAvoidingPollStarvationButDontWait();
    void SetReadError(PRegister reg);
    PRegisterHandler GetHandler(PRegister) const;
    void SetRegistersAvailability(PSerialDevice dev, TRegisterAvailability availability);
    void ClosedPortCycle();
    void OpenPortCycle();
    void UpdateFlushNeeded();
    void ProcessPolledRegister(PRegister reg);
    void ScheduleNextPoll(PRegister reg, std::chrono::steady_clock::time_point pollStartTime);
    std::vector<PRegisterRange> ReadRanges(TRegisterReader& reader,
                                           PRegisterRange range,
                                           std::chrono::steady_clock::time_point pollStartTime,
                                           bool forceError);
    void RPCRequestHandling();

    PPort Port;
    std::list<PRegister> RegList;
    std::vector<PSerialDevice> Devices;
    std::unordered_map<PRegister, PRegisterHandler> Handlers;

    bool Active;
    TCallback ReadCallback;
    TCallback ErrorCallback;
    PSerialDevice LastAccessedDevice;
    PBinarySemaphore FlushNeeded;
    TScheduler<PRegister, TRegisterComparePredicate, TPreemptivePolicy> Scheduler;

    TPortOpenCloseLogic OpenCloseLogic;
    TLoggerWithTimeout ConnectLogger;
    Metrics::TMetrics& Metrics;

    std::mutex RPCMutex;
    std::vector<uint8_t> RPCWriteData, RPCReadData;
    size_t RPCRequestedSize, RPCActualSize;
    std::chrono::milliseconds RPCRespTimeout;
    std::chrono::milliseconds RPCFrameTimeout;
    enum ERPCState RPCState = RPC_IDLE;
};

typedef std::shared_ptr<TSerialClient> PSerialClient;
