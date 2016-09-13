#pragma once
#include <cmath>
#include <mutex>
#include <memory>
#include <wbmqtt/utils.h>
#include "register.h"
#include "serial_device.h"
#include "binary_semaphore.h"

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
	template<typename T> std::string ToScaledTextValue(T val) const;
	template<typename T> T FromScaledTextValue(const std::string& str) const;
	uint64_t ConvertMasterValue(const std::string& v) const;
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
    bool Debug;
};

template<>
inline std::string TRegisterHandler::ToScaledTextValue(float val) const
{
    return StringFormat("%.7g", Reg->Scale * val);
}

template<>
inline std::string TRegisterHandler::ToScaledTextValue(double val) const
{
    return StringFormat("%.15g", Reg->Scale * val);
}

template<typename A>
inline std::string TRegisterHandler::ToScaledTextValue(A val) const
{
    if (Reg->Scale == 1) {
        return std::to_string(val);
    } else {
        return StringFormat("%.15g", Reg->Scale * val);
    }
}

template<>
inline uint64_t TRegisterHandler::FromScaledTextValue(const std::string& str) const
{
    if (Reg->Scale == 1) {
		return std::stoull(str);
    } else {
        return round(stod(str) / Reg->Scale);
	}
}

template<>
inline int64_t TRegisterHandler::FromScaledTextValue(const std::string& str) const
{
    if (Reg->Scale == 1) {
		return std::stoll(str);
    } else {
        return round(stod(str) / Reg->Scale);
	}
}

template<>
inline double TRegisterHandler::FromScaledTextValue(const std::string& str) const
{
    return stod(str) / Reg->Scale;
}

typedef std::shared_ptr<TRegisterHandler> PRegisterHandler;
