#include "register_handler.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[register handler] "

TRegisterHandler::TRegisterHandler(PSerialDevice dev, PRegister reg, PBinarySemaphore flush_needed)
    : Dev(dev), Reg(reg), FlushNeeded(flush_needed){}

TRegisterHandler::TErrorState TRegisterHandler::UpdateReadError(bool error) {
    TErrorState newState;
    if (error) {
        newState = ErrorState == WriteError ||
            ErrorState == ReadWriteError ?
            ReadWriteError : ReadError;
    } else {
        newState = ErrorState == ReadWriteError ||
            ErrorState == WriteError ?
            WriteError : NoError;
    }

    if (ErrorState == newState)
        return ErrorStateUnchanged;
    ErrorState = newState;
    return ErrorState;
}

TRegisterHandler::TErrorState TRegisterHandler::UpdateWriteError(bool error)
{
    TErrorState newState;
    if (error) {
        newState = ErrorState == ReadError ||
            ErrorState == ReadWriteError ?
            ReadWriteError : WriteError;
    } else {
        newState = ErrorState == ReadWriteError ||
            ErrorState == ReadError ?
            ReadError : NoError;
    }

    if (ErrorState == newState)
        return ErrorStateUnchanged;
    ErrorState = newState;
    return ErrorState;
}

bool TRegisterHandler::NeedToPoll()
{
    std::lock_guard<std::mutex> lock(SetValueMutex);
    return Reg->Poll && !Dirty;
}

TRegisterHandler::TErrorState TRegisterHandler::AcceptDeviceValue(uint64_t new_value, bool ok, bool *changed)
{
    *changed = false;

    if (!ok)
        return UpdateReadError(true);

    bool first_poll = !DidReadReg;
    DidReadReg = true;

    if (Reg->ErrorValue && InvertWordOrderIfNeeded(*Reg, *Reg->ErrorValue) == new_value) {
        LOG(Debug) << "register " << Reg->ToString() << " contains error value";
        return UpdateReadError(true);
    }

    SetValueMutex.lock();
    if (Value != new_value) {
        if (Dirty) {
            SetValueMutex.unlock();
            return UpdateReadError(false);
        }

        Value = new_value;
        SetValueMutex.unlock();

        LOG(Debug) << "new val for " << Reg->ToString() << ": " << std::hex << new_value;

        *changed = true;
        return UpdateReadError(false);
    } else
        SetValueMutex.unlock();

    *changed = first_poll;
    return UpdateReadError(false);
}

bool TRegisterHandler::NeedToFlush()
{
    std::lock_guard<std::mutex> lock(SetValueMutex);
    return Dirty;
}

TRegisterHandler::TErrorState TRegisterHandler::Flush(TErrorState forcedError)
{
    if (!NeedToFlush())
        return ErrorStateUnchanged;

    {
        std::lock_guard<std::mutex> lock(SetValueMutex);
        Dirty = false;
    }

    if (forcedError == WriteError) {
        return UpdateWriteError(true);
    }

    try {
        Device()->WriteRegister(Reg, Value);
    } catch (const TSerialDeviceTransientErrorException& e) {
        LOG(Warn) << "failed to write: " << Reg->ToString() << ": " << e.what();
        return UpdateWriteError(true);
    } catch (const TSerialDevicePermanentRegisterException& e) {
        LOG(Warn) << "failed to write: " << Reg->ToString() << ": " << e.what();
        return UpdateWriteError(true);
    }
    return UpdateWriteError(false);
}

std::string TRegisterHandler::TextValue() const
{
    return ConvertFromRawValue(*Reg, Value);
}

void TRegisterHandler::SetTextValue(const std::string& v)
{
    {
        // don't hold the lock while notifying the client below
        std::lock_guard<std::mutex> lock(SetValueMutex);
        Dirty = true;
        Value = ConvertToRawValue(*Reg, v);
    }
    FlushNeeded->Signal();
}
