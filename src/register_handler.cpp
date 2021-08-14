#include "register_handler.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[register handler] "

using namespace std::chrono;

namespace 
{
    const size_t MAX_WRITE_FAILS = 10;
    const seconds MAX_WRITE_FAIL_TIME(600); // 10 minutes
}

TRegisterHandler::TRegisterHandler(PSerialDevice dev, PRegister reg, PBinarySemaphore flush_needed)
    : Dev(dev), Reg(reg), FlushNeeded(flush_needed), WriteFail(false)
{}

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
    return Reg->Poll;
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

    if (OldValue != new_value) {
        OldValue = new_value;
        SetValueMutex.unlock();

        LOG(Debug) << "new val for " << Reg->ToString() << ": " << std::hex << new_value;
        *changed = true;
        return UpdateReadError(false);
    }

    SetValueMutex.unlock();

    *changed = first_poll;
    return UpdateReadError(false);
}

bool TRegisterHandler::NeedToFlush()
{
    std::lock_guard<std::mutex> lock(SetValueMutex);
    return Dirty;
}

TRegisterHandler::TFlushResult TRegisterHandler::Flush(TErrorState forcedError)
{
    if (forcedError == WriteError) {
        return { UpdateWriteError(true), false };
    }

    bool changed = false;
    volatile uint64_t tempValue;
    try {
        {
            std::lock_guard<std::mutex> lock(SetValueMutex);
            tempValue = ValueToSet;
        }
        Device()->WriteRegister(Reg, tempValue);
        {
            std::lock_guard<std::mutex> lock(SetValueMutex);
            Dirty = (tempValue != ValueToSet);
            WriteFail = false;
        }
        changed = (OldValue != tempValue);
        OldValue = tempValue;
        Reg->SetValue(OldValue);
    } catch (const TSerialDeviceTransientErrorException& e) {
        LOG(Warn) << "failed to write: " << Reg->ToString() << ": " << e.what();
        {
            std::lock_guard<std::mutex> lock(SetValueMutex);
            if (!WriteFail) {
                WriteFirstTryTime = steady_clock::now();
            }
            WriteFail = true;
            if (duration_cast<seconds>(steady_clock::now() - WriteFirstTryTime) > MAX_WRITE_FAIL_TIME) {
                Dirty = (tempValue != ValueToSet);
                WriteFail = false;
            }
        }
        return { UpdateWriteError(true), false };
    } catch (const TSerialDevicePermanentRegisterException& e) {
        LOG(Warn) << "failed to write: " << Reg->ToString() << ": " << e.what();
        {
            std::lock_guard<std::mutex> lock(SetValueMutex);
            Dirty = (tempValue != ValueToSet);
            WriteFail = false;
        }
        return { UpdateWriteError(true), false };
    }
    return { UpdateWriteError(false), changed };
}

std::string TRegisterHandler::TextValue() const
{
    return ConvertFromRawValue(*Reg, Reg->GetValue());
}

void TRegisterHandler::SetTextValue(const std::string& v)
{
    {
        // don't hold the lock while notifying the client below
        std::lock_guard<std::mutex> lock(SetValueMutex);
        Dirty = true;
        ValueToSet = ConvertToRawValue(*Reg, v);
    }
    FlushNeeded->Signal();
}
