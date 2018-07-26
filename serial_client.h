#pragma once

#include "poll_plan.h"
#include "ir_device_query_handler.h"
#include "utils.h"


class TSerialClient: public std::enable_shared_from_this<TSerialClient>
{
public:
    using TRegisterCallback = std::function<void(const PVirtualRegister &)>;

    TSerialClient(PPort port);
    TSerialClient(const TSerialClient& client) = delete;
    TSerialClient& operator=(const TSerialClient&) = delete;
    ~TSerialClient();

    PSerialDevice CreateDevice(PDeviceConfig device_config);
    void AddRegister(PVirtualRegister reg);
    void Connect();
    void Disconnect();
    void Cycle();
    void SetReadCallback(TRegisterCallback callback);
    void SetErrorCallback(TRegisterCallback callback);
    void SetDebug(bool debug);
    bool DebugEnabled() const;
    void NotifyFlushNeeded();
    bool WriteSetupRegisters(PSerialDevice dev);

private:
    void InitializeMemoryBlocksCache();
    void GenerateReadQueries();
    bool DoFlush();
    bool WaitForPollAndFlush();
    void MaybeFlushAvoidingPollStarvationButDontWait(bool & needFlush);
    void MaybeUpdateErrorState(PVirtualRegister reg);
    void PrepareToAccessDevice(PSerialDevice dev);
    void OnDeviceReconnect(PSerialDevice dev);

    using TVirtualRegisterMap = std::unordered_map<PSerialDevice, std::vector<PVirtualRegister>>;

    TVirtualRegisterMap      VirtualRegisters;
    std::list<PSerialDevice> DevicesList; /* for EndPollCycle */
    TIRDeviceQuerySetHandler QuerySetHandler;
    TRegisterCallback        ReadCallback;
    TRegisterCallback        ErrorCallback;
    PPort                    Port;
    PSerialDevice            LastAccessedDevice;
    PBinarySemaphore         FlushNeeded;
    PPollPlan                Plan;
    int                      PollInterval;
    bool                     Debug;
    bool                     Active;
};
