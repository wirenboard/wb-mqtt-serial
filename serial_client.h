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
    typedef std::function<void(PRegister reg)> TCallback;
    typedef std::function<void(PRegister reg, TRegisterHandler::TErrorState errorState)> TErrorCallback;

    TSerialClient(PAbstractSerialPort port);
    TSerialClient(const TSerialClient& client) = delete;
    TSerialClient& operator=(const TSerialClient&) = delete;
    ~TSerialClient();

    void AddDevice(PDeviceConfig device_config);
    void AddRegister(PRegister reg);
    void Connect();
    void Disconnect();
    void Cycle();
    void SetTextValue(PRegister reg, const std::string& value);
    std::string GetTextValue(PRegister reg) const;
    bool DidRead(PRegister reg) const;
    void SetCallback(const TCallback& callback);
    void SetErrorCallback(const TErrorCallback& callback);
    void SetDebug(bool debug);
    bool DebugEnabled() const;
    void NotifyFlushNeeded();
    void WriteSetupRegister(PRegister reg, uint64_t value);

private:
    void PrepareRegisterRanges();
    void DoFlush();
    void WaitForPollAndFlush();
    void MaybeFlushAvoidingPollStarvationButDontWait();
    void PollRange(PRegisterRange range);
    PRegisterHandler GetHandler(PRegister) const;
    void MaybeUpdateErrorState(PRegister reg, TRegisterHandler::TErrorState state);
    PSerialDevice GetDevice(PSlaveEntry entry);
    void PrepareToAccessDevice(PSerialDevice dev);

    PAbstractSerialPort Port;
    std::list<PRegister> RegList;
    std::unordered_map<PRegister, PRegisterHandler> Handlers;
    std::unordered_map<PSlaveEntry, PDeviceConfig> ConfigMap;
    std::unordered_map<PSlaveEntry, PSerialDevice> DeviceMap;

    bool Active;
    TCallback Callback;
    TErrorCallback ErrorCallback;
    bool Debug = false;
    PSerialDevice LastAccessedDevice = 0;
    PBinarySemaphore FlushNeeded;
    PPollPlan Plan;

    const int MAX_REGS = 65536;
    const int MAX_FLUSHES_WHEN_POLL_IS_DUE = 20;
};

typedef std::shared_ptr<TSerialClient> PSerialClient;
