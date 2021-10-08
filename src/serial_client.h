#pragma once

#include <list>
#include <memory>
#include <functional>
#include <unordered_map>

#include "poll_plan.h"
#include "serial_device.h"
#include "register_handler.h"
#include "binary_semaphore.h"
#include "log.h"
#include "metrics.h"

class TSerialClient: public std::enable_shared_from_this<TSerialClient>
{
public:
    typedef std::function<void(PRegister reg, bool changed)>                             TReadCallback;
    typedef std::function<void(PRegister reg, TRegisterHandler::TErrorState errorState)> TErrorCallback;

    TSerialClient(const std::vector<PSerialDevice>&     devices,
                  PPort                                 port,
                  const TPortOpenCloseLogic::TSettings& openCloseSettings,
                  Metrics::TMetrics&                    metrics);
    TSerialClient(const TSerialClient& client) = delete;
    TSerialClient& operator=(const TSerialClient&) = delete;
    ~TSerialClient();

    void        AddRegister(PRegister reg);
    void        Cycle();
    void        SetTextValue(PRegister reg, const std::string& value);
    std::string GetTextValue(PRegister reg) const;
    bool        DidRead(PRegister reg) const;
    void        SetReadCallback(const TReadCallback& callback);
    void        SetErrorCallback(const TErrorCallback& callback);
    void        NotifyFlushNeeded();
    void        ClearDevices();

private:
    void                      Activate();
    void                      Connect();
    void                      PrepareRegisterRanges();
    void                      DoFlush();
    void                      WaitForPollAndFlush();
    void                      MaybeFlushAvoidingPollStarvationButDontWait();
    std::list<PRegisterRange> PollRange(PRegisterRange range);
    void                      SetReadError(PRegisterRange range);
    PRegisterHandler          GetHandler(PRegister) const;
    void                      MaybeUpdateErrorState(PRegister reg, TRegisterHandler::TErrorState state);
    void                      PrepareToAccessDevice(PSerialDevice dev);
    void                      OnDeviceReconnect(PSerialDevice dev);
    void                      ClosedPortCycle();
    void                      OpenPortCycle();
    void                      UpdateFlushNeeded();

    PPort                                           Port;
    std::list<PRegister>                            RegList;
    std::vector<PSerialDevice>                      Devices; /* for EndPollCycle */
    std::unordered_map<PRegister, PRegisterHandler> Handlers;

    bool             Active;
    int              PollInterval;
    TReadCallback    ReadCallback;
    TErrorCallback   ErrorCallback;
    PSerialDevice    LastAccessedDevice = 0;
    PBinarySemaphore FlushNeeded;
    PPollPlan        Plan;

    const int MAX_REGS                     = 65536;
    const int MAX_FLUSHES_WHEN_POLL_IS_DUE = 20;

    TPortOpenCloseLogic OpenCloseLogic;
    TLoggerWithTimeout  ConnectLogger;
    Metrics::TMetrics&  Metrics;
};

typedef std::shared_ptr<TSerialClient> PSerialClient;
