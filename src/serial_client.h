#pragma once

#include "binary_semaphore.h"
#include "log.h"
#include "modbus_ext_common.h"
#include "poll_plan.h"
#include "register_handler.h"
#include "rpc_request_handler.h"
#include <functional>
#include <list>
#include <memory>
#include <unordered_map>

class TSerialDevice;
typedef std::shared_ptr<TSerialDevice> PSerialDevice;

struct TRegisterComparePredicate
{
    bool operator()(const PRegister& r1, const PRegister& r2) const;
};

class TThrottlingStateLogger
{
    bool FirstTime;

public:
    TThrottlingStateLogger();

    std::string GetMessage(TThrottlingState state);
};

class TSerialClient: public std::enable_shared_from_this<TSerialClient>
{
public:
    typedef std::function<void(PRegister reg)> TCallback;

    TSerialClient(const std::vector<PSerialDevice>& devices,
                  PPort port,
                  const TPortOpenCloseLogic::TSettings& openCloseSettings,
                  size_t lowPriorityRateLimit = std::numeric_limits<size_t>::max());
    TSerialClient(const TSerialClient& client) = delete;
    TSerialClient& operator=(const TSerialClient&) = delete;
    ~TSerialClient();

    void AddRegister(PRegister reg);
    void Cycle();
    void SetTextValue(PRegister reg, const std::string& value);
    void SetReadCallback(const TCallback& callback);
    void SetErrorCallback(const TCallback& callback);
    void ClearDevices();
    PPort GetPort();
    void RPCTransceive(PRPCRequest request) const;
    PRegister FindRegister(uint8_t slaveId, uint16_t addr) const;
    void ProcessPolledRegister(PRegister reg);

private:
    void Activate();
    void Connect();
    void PrepareRegisterRanges();
    void DoFlush();
    void WaitForPollAndFlush(std::chrono::steady_clock::time_point waitUntil);
    void SetReadError(PRegister reg);
    PRegisterHandler GetHandler(PRegister) const;
    void SetRegistersAvailability(PSerialDevice dev, TRegisterAvailability availability);
    void ReadEvents();
    void ClosedPortCycle();
    void OpenPortCycle();
    void ScheduleNextPoll(PRegister reg, std::chrono::steady_clock::time_point pollStartTime);
    void UpdateSelectionTime(std::chrono::steady_clock::time_point currentTime);
    void UpdateFlushNeeded();
    PPort Port;
    std::list<PRegister> RegList;
    std::vector<PSerialDevice> Devices;
    std::unordered_map<PRegister, PRegisterHandler> Handlers;

    bool Active;
    ModbusExt::TEventConfirmationState EventState;
    TCallback ReadCallback;
    TCallback ErrorCallback;
    PSerialDevice LastAccessedDevice;
    PBinarySemaphore FlushNeeded;
    PBinarySemaphoreSignal RegisterUpdateSignal, RPCSignal;
    TScheduler<PRegister, TRegisterComparePredicate> Scheduler;

    TPortOpenCloseLogic OpenCloseLogic;
    TLoggerWithTimeout ConnectLogger;
    TThrottlingStateLogger ThrottlingStateLogger;

    PRPCRequestHandler RPCRequestHandler;
    std::chrono::steady_clock::time_point LastQueueSelectionTime;

    std::chrono::steady_clock::time_point NextEventsRead;
};

typedef std::shared_ptr<TSerialClient> PSerialClient;
