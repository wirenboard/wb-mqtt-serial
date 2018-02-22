#pragma once

#include <list>
#include <memory>
#include <functional>
#include <unordered_map>

#include "poll_plan.h"
#include "serial_device.h"
#include "register_handler.h"
#include "binary_semaphore.h"

class TSerialClient: public std::enable_shared_from_this<TSerialClient>
{
public:
    typedef std::function<void(PVirtualRegister reg, bool changed)> TReadCallback;
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
    void SetTextValue(PRegister reg, const std::string& value);
    std::string GetTextValue(PRegister reg) const;
    bool DidRead(PRegister reg) const;
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
    void PollRange(PRegisterRange range);
    void ExecuteQuery(const PIRDeviceQuery & query);
    PRegisterHandler GetHandler(PRegister) const;
    void MaybeUpdateErrorState(PVirtualRegister reg);
    void PrepareToAccessDevice(PSerialDevice dev);
    void OnDeviceReconnect(PSerialDevice dev);
    void SplitRegisterRanges(std::set<PRegisterRange> &&);

    PPort Port;
    TPSet<TVirtualRegister> VirtualRegisters;
    std::unordered_map<PSerialDevice, std::set<PProtocolRegister, utils::ptr_cmp<TProtocolRegister>>> ProtocolRegisters;
    std::list<PSerialDevice> DevicesList; /* for EndPollCycle */
    std::unordered_map<PRegister, PRegisterHandler> Handlers;

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
