#pragma once

#include <list>
#include <memory>
#include <functional>
#include <unordered_map>

#include "poll_plan.h"
#include "serial_device.h"
#include "binary_semaphore.h"
#include "utils.h"

class TSerialClient: public std::enable_shared_from_this<TSerialClient>
{
public:
    typedef std::function<void(PVirtualRegister reg)> TReadCallback;
    typedef std::function<void(PVirtualRegister reg)> TErrorCallback;

    TSerialClient(PPort port);
    TSerialClient(const TSerialClient& client) = delete;
    TSerialClient& operator=(const TSerialClient&) = delete;
    ~TSerialClient();

    PSerialDevice CreateDevice(PDeviceConfig device_config);
    void AddRegister(PVirtualRegister reg);
    void Connect();
    void Disconnect();
    void Cycle();
    void SetReadCallback(const TReadCallback& callback);
    void SetErrorCallback(const TErrorCallback& callback);
    void SetDebug(bool debug);
    bool DebugEnabled() const;
    void NotifyFlushNeeded();
    bool WriteSetupRegisters(PSerialDevice dev);

private:
    void PrepareRegisterRanges();
    void GenerateReadQueries();
    void DoFlush();
    void WaitForPollAndFlush();
    void MaybeFlushAvoidingPollStarvationButDontWait();
    void MaybeUpdateErrorState(PVirtualRegister reg);
    void PrepareToAccessDevice(PSerialDevice dev);
    void OnDeviceReconnect(PSerialDevice dev);

    PPort Port;
    std::unordered_map<PSerialDevice, TPUnorderedSet<PVirtualRegister>> VirtualRegisters;
    std::list<PSerialDevice> DevicesList; /* for EndPollCycle */

    bool Active;
    int PollInterval;
    TReadCallback ReadCallback;
    TErrorCallback ErrorCallback;
    bool Debug = false;
    PSerialDevice LastAccessedDevice = 0;
    PBinarySemaphore FlushNeeded;
    PPollPlan Plan;

    const int MAX_REGS = 65536;
    const int MAX_FLUSHES_WHEN_POLL_IS_DUE = 20;
};

typedef std::shared_ptr<TSerialClient> PSerialClient;
