#include <iomanip>
#include <iostream>

#include "register_handler.h"

TRegisterHandler::TRegisterHandler(PSerialDevice dev, PRegister reg, PBinarySemaphore flush_needed, bool debug)
    : Dev(dev), Reg(reg), FlushNeeded(flush_needed), Debug(debug) {}

TRegisterHandler::TErrorState TRegisterHandler::UpdateReadError(bool error) {

}

TRegisterHandler::TErrorState TRegisterHandler::UpdateWriteError(bool error) {

}

bool TRegisterHandler::NeedToPoll()
{
    std::lock_guard<std::mutex> lock(SetValueMutex);
    return Reg->Poll && !Dirty;
}

TRegisterHandler::TErrorState TRegisterHandler::AcceptDeviceValue(uint64_t new_value, bool ok, bool *changed)
{
    *changed = false;

    // don't poll write-only and dirty registers
    if (!NeedToPoll())
        return ErrorStateUnchanged;

    if (!ok)
        return UpdateReadError(true);


   bool firs t_poll = !DidReadReg;
    DidReadReg = true;

    if (Reg->HasErrorValue && Reg->ErrorValue == new_value) {
        if (Debug) {
            std::cerr << "register " << Reg->ToString() << " contains error value" << std::endl;
        }
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

        if (Debug) {
            std::ios::fmtflags f(std::cerr.flags());
            std::cerr << "new val for " << Reg->ToString() << ": " << std::hex << new_value << std::endl;
            std::cerr.flags(f);
        }
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

TRegisterHandler::TErrorState TRegisterHandler::Flush()
{
    if (!NeedToFlush())
        return ErrorStateUnchanged;

    {
        std::lock_guard<std::mutex> lock(SetValueMutex);
        Dirty = false;
    }

    try {
        Device()->WriteRegister(Reg, Value);
    } catch (const TSerialDeviceTransientErrorException& e) {
        std::ios::fmtflags f(std::cerr.flags());
        std::cerr << "TRegisterHandler::Flush(): warning: " << e.what() << " for device " <<
            Reg->Device()->ToString() <<  std::endl;
        std::cerr.flags(f);
        return UpdateWriteError(true);
    }
    return UpdateWriteError(false);
}



std::string TRegisterHandler::TextValue() const
{
    return ConvertSlaveValue(InvertWordOrderIfNeeded(Value));
}



void TRegisterHandler::SetTextValue(const std::string& v)
{
    {
        // don't hold the lock while notifying the client below
        std::lock_guard<std::mutex> lock(SetValueMutex);
        Dirty = true;
        Value = InvertWordOrderIfNeeded(ConvertMasterValue(v));
    }
    FlushNeeded->Signal();
}
