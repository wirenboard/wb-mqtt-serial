#pragma once
#include <cmath>
#include <mutex>
#include <memory>
#include "register.h"
#include "serial_protocol.h"

class IDebugEnabled {
public:
    virtual ~IDebugEnabled() {}
    virtual bool DebugEnabled() const = 0;
};

typedef std::shared_ptr<IDebugEnabled> PDebugEnabled;
    
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
    TRegisterHandler(PDebugEnabled debugState, PSerialProtocol proto, PRegister reg);
    PRegister Register() const { return Reg; }
    TErrorState Poll(bool* changed);
    TErrorState Flush();
    std::string TextValue() const;

    void SetTextValue(const std::string& v);
    bool DidRead() const { return DidReadReg; }
    TErrorState CurrentErrorState() const { return ErrorState; }

private:
	template<typename T> std::string ToScaledTextValue(T val) const;
	template<typename T> T FromScaledTextValue(const std::string& str) const;
	uint64_t ConvertMasterValue(const std::string& v) const;
    TErrorState UpdateReadError(bool error);
    TErrorState UpdateWriteError(bool error);

    PDebugEnabled DebugState;
    PSerialProtocol Proto;
    uint64_t Value = 0;
    PRegister Reg;
    volatile bool Dirty = false;
    bool DidReadReg = false;
    std::mutex SetValueMutex;
    TErrorState ErrorState = UnknownErrorState;
};

template<typename A>
inline std::string TRegisterHandler::ToScaledTextValue(A val) const
{
	if (Reg->Scale == 1) {
		return std::to_string(val);
	} else {
		return std::to_string(Reg->Scale * val);
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
