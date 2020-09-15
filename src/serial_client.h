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
    typedef std::function<void(PRegister reg, bool changed)> TReadCallback;
    typedef std::function<void(PRegister reg, TRegisterHandler::TErrorState errorState)> TErrorCallback;

    TSerialClient(PPort port);
    TSerialClient(const TSerialClient& client) = delete;
    TSerialClient& operator=(const TSerialClient&) = delete;
    ~TSerialClient();

    PSerialDevice CreateDevice(PDeviceConfig device_config);
    void AddRegister(PRegister reg);
    void Connect();
    void Disconnect();
    void Cycle();
    void SetTextValue(PRegister reg, const std::string& value);
    std::string GetTextValue(PRegister reg) const;
    bool DidRead(PRegister reg) const;
    void SetReadCallback(const TReadCallback& callback);
    void SetErrorCallback(const TErrorCallback& callback);
    void NotifyFlushNeeded();
    bool WriteSetupRegisters(PSerialDevice dev);
    void ClearDevices();

private:
    void PrepareRegisterRanges();
    void DoFlush();
    void WaitForPollAndFlush();
    void MaybeFlushAvoidingPollStarvationButDontWait();
    void PollRange(PRegisterRange range);
    PRegisterHandler GetHandler(PRegister) const;
    void MaybeUpdateErrorState(PRegister reg, TRegisterHandler::TErrorState state);
    void PrepareToAccessDevice(PSerialDevice dev);
    void OnDeviceReconnect(PSerialDevice dev);
    void SplitRegisterRanges(std::set<PRegisterRange> &&);

    PPort Port;
    std::list<PRegister> RegList;
    std::list<PSerialDevice> DevicesList; /* for EndPollCycle */
    std::unordered_map<PRegister, PRegisterHandler> Handlers;

    bool Active;
    int PollInterval;
    TReadCallback ReadCallback;
    TErrorCallback ErrorCallback;
    PSerialDevice LastAccessedDevice = 0;
    PBinarySemaphore FlushNeeded;
    PPollPlan Plan;

    const int MAX_REGS = 65536;
    const int MAX_FLUSHES_WHEN_POLL_IS_DUE = 20;
};

typedef std::shared_ptr<TSerialClient> PSerialClient;
