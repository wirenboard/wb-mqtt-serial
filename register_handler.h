#pragma once
#include <cmath>
#include <mutex>
#include <memory>
#include <string>
#include <wbmqtt/utils.h>
#include "register.h"
#include "serial_device.h"
#include "binary_semaphore.h"
#include "bcd_utils.h"

class TRegisterHandler
{
public:

    TRegisterHandler(PSerialDevice dev, PRegister reg, PBinarySemaphore flush_needed, bool debug = false);
    PRegister Register() const { return Reg; }
    bool NeedToPoll();
    TErrorState AcceptDeviceValue(uint64_t new_value, bool ok, bool* changed);
    bool NeedToFlush();
    TErrorState Flush();
    std::string TextValue() const;

    void SetTextValue(const std::string& v);
    bool DidRead() const { return DidReadReg; }
    TErrorState CurrentErrorState() const { return ErrorState; }
    PSerialDevice Device() const { return Dev.lock(); }
    void SetDebug(bool debug) { Debug = debug; }

private:
    template<typename T> static T RoundValue(T val, double round_to);
    template<typename T> std::string ToScaledTextValue(T val) const;
    template<typename T> T FromScaledTextValue(const std::string& str) const;
    uint64_t ConvertMasterValue(const std::string& v) const;
    std::string ConvertSlaveValue(uint64_t value) const;

    uint64_t InvertWordOrderIfNeeded(const uint64_t value) const;

    std::weak_ptr<TSerialDevice> Dev;
    uint64_t Value = 0;
    PRegister Reg;
    volatile bool Dirty = false;
    bool DidReadReg = false;
    std::mutex SetValueMutex;
    TErrorState ErrorState = UnknownErrorState;

    bool Debug;
};


typedef std::shared_ptr<TRegisterHandler> PRegisterHandler;
