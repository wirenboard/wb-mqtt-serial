#include <iomanip>
#include <iostream>

#include "register_handler.h"

TRegisterHandler::TRegisterHandler(PClientInteraction clientInteraction, PSerialDevice dev, PRegister reg)
    : ClientInteraction(clientInteraction), Dev(dev), Reg(reg) {}

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

TRegisterHandler::TErrorState TRegisterHandler::UpdateWriteError(bool error) {
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

TRegisterHandler::TErrorState TRegisterHandler::Poll(bool* changed)
{
    *changed = false;

    // don't poll write-only and dirty registers
    if (!NeedToPoll())
        return ErrorStateUnchanged;

    bool first_poll = !DidReadReg;
    uint64_t new_value;
    try {
        new_value = Dev->ReadRegister(Reg);
    } catch (const TSerialDeviceTransientErrorException& e) {
        std::ios::fmtflags f(std::cerr.flags());
        std::cerr << "TRegisterHandler::Poll(): warning: " << e.what() << " [slave_id is "
				  << Reg->Slave->Id << "(0x" << std::hex << Reg->Slave->Id << ")]" << std::endl;
        std::cerr.flags(f);
        return UpdateReadError(true);
    }

    DidReadReg = true;
    SetValueMutex.lock();
    if (Value != new_value) {
        if (Dirty) {
            SetValueMutex.unlock();
            return UpdateReadError(false);
        }

        Value = new_value;
        SetValueMutex.unlock();

        if (ClientInteraction->DebugEnabled()) {
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
        Dev->WriteRegister(Reg, Value);
    } catch (const TSerialDeviceTransientErrorException& e) {
        std::ios::fmtflags f(std::cerr.flags());
        std::cerr << "TRegisterHandler::Flush(): warning: " << e.what() << " slave_id is " <<
            Reg->Slave->Id << "(0x" << std::hex << Reg->Slave->Id << ")" <<  std::endl;
        std::cerr.flags(f);
        return UpdateWriteError(true);
    }
    return UpdateWriteError(false);
}

std::string TRegisterHandler::TextValue() const
{
    switch (Reg->Format) {
    case S8:
        return ToScaledTextValue(int8_t(Value & 0xff));
    case S16:
        return ToScaledTextValue(int16_t(Value & 0xffff));
    case S24:
        {
            uint32_t v = Value & 0xffffff;
            if (v & 0x800000) // fix sign (TBD: test)
                v |= 0xff000000;
            return ToScaledTextValue(int32_t(v));
        }
    case S32:
        return ToScaledTextValue(int32_t(Value & 0xffffffff));
    case S64:
        return ToScaledTextValue(int64_t(Value));
	case Float:
        {
            union {
                uint32_t raw;
                float v;
            } tmp;
            tmp.raw = Value;
            return ToScaledTextValue(tmp.v);
        }
    case Double:
		{
            union {
                uint64_t raw;
                double v;
            } tmp;
            tmp.raw = Value;
            return ToScaledTextValue(tmp.v);
		}
    default:
        return ToScaledTextValue(Value);
    }
}

void TRegisterHandler::SetTextValue(const std::string& v)
{
    {
        // don't hold the lock while notifying the client below
        std::lock_guard<std::mutex> lock(SetValueMutex);
        Dirty = true;
        Value = ConvertMasterValue(v);
    }
    ClientInteraction->NotifyFlushNeeded();
}

uint64_t TRegisterHandler::ConvertMasterValue(const std::string& str) const
{
    switch (Reg->Format) {
    case S8:
        return FromScaledTextValue<int64_t>(str) & 0xff;
    case S16:
        return FromScaledTextValue<int64_t>(str) & 0xffff;
    case S24:
        return FromScaledTextValue<int64_t>(str) & 0xffffff;
    case S32:
        return FromScaledTextValue<int64_t>(str) & 0xffffffff;
    case S64:
        return FromScaledTextValue<int64_t>(str);
    case U8:
        return FromScaledTextValue<uint64_t>(str) & 0xff;
    case U16:
        return FromScaledTextValue<uint64_t>(str) & 0xffff;
    case U24:
        return FromScaledTextValue<uint64_t>(str) & 0xffffff;
    case U32:
        return FromScaledTextValue<uint64_t>(str) & 0xffffffff;
    case U64:
        return FromScaledTextValue<uint64_t>(str);
	case Float:
		{
            union {
                uint32_t raw;
                float v;
            } tmp;

			tmp.v = FromScaledTextValue<double>(str);
            return tmp.raw;
		}
	case Double:
		{
            union {
                uint64_t raw;
                double v;
            } tmp;
			tmp.v = FromScaledTextValue<double>(str);
            return tmp.raw;
		}
    default:
        return FromScaledTextValue<uint64_t>(str);
    }
}
