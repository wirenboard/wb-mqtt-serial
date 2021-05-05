#pragma once
#include <cmath>
#include <mutex>
#include <memory>
#include <string>
#include <wblib/utils.h>
#include "register.h"
#include "serial_device.h"
#include "binary_semaphore.h"
#include "bcd_utils.h"

using WBMQTT::StringFormat;

class TRegisterHandler
{
public:
    enum TErrorState {
        NoError,
        WriteError,
        ReadError,
        ReadWriteError,
        UnknownErrorState,
        ErrorStateUnchanged
    };
    TRegisterHandler(PSerialDevice dev, PRegister reg, PBinarySemaphore flush_needed);
    PRegister Register() const { return Reg; }
    bool NeedToPoll();
    TErrorState AcceptDeviceValue(uint64_t new_value, bool ok, bool* changed);
    bool NeedToFlush();
    TErrorState Flush(TErrorState forcedError = NoError);
    std::string TextValue() const;

    void SetTextValue(const std::string& v);
    bool DidRead() const { return DidReadReg; }
    TErrorState CurrentErrorState() const { return ErrorState; }
    PSerialDevice Device() const { return Dev.lock(); }

private:
    TErrorState UpdateReadError(bool error);
    TErrorState UpdateWriteError(bool error);

    std::weak_ptr<TSerialDevice> Dev;
    uint64_t Value = 0;
    PRegister Reg;
    volatile bool Dirty = false;
    bool DidReadReg = false;
    std::mutex SetValueMutex;
    TErrorState ErrorState = UnknownErrorState;
    PBinarySemaphore FlushNeeded;
};

typedef std::shared_ptr<TRegisterHandler> PRegisterHandler;
