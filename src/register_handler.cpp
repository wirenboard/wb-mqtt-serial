#include "register_handler.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[register handler] "

using namespace std::chrono;

namespace
{
    const seconds MAX_WRITE_FAIL_TIME(600); // 10 minutes
}

TRegisterHandler::TRegisterHandler(PSerialDevice dev, PRegister reg): Dev(dev), Reg(reg), WriteFail(false)
{}

bool TRegisterHandler::NeedToFlush()
{
    std::lock_guard<std::mutex> lock(SetValueMutex);
    return Dirty;
}

void TRegisterHandler::Flush()
{
    TRegisterValue tempValue; // must be volatile
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
        Reg->SetValue(tempValue, false);
        Reg->ClearError(TRegister::TError::WriteError);
    } catch (const TSerialDevicePermanentRegisterException& e) {
        LOG(Warn) << "failed to write: " << Reg->ToString() << ": " << e.what();
        {
            std::lock_guard<std::mutex> lock(SetValueMutex);
            Dirty = (tempValue != ValueToSet);
            WriteFail = false;
        }
        Reg->SetError(TRegister::TError::WriteError);
        return;
    } catch (const TSerialDeviceException& e) {
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
        Reg->SetError(TRegister::TError::WriteError);
    }
}

void TRegisterHandler::SetTextValue(const std::string& v)
{
    // don't hold the lock while notifying the client below
    std::lock_guard<std::mutex> lock(SetValueMutex);
    Dirty = true;
    ValueToSet = ConvertToRawValue(*Reg, v);
}

PRegister TRegisterHandler::Register() const
{
    return Reg;
}

PSerialDevice TRegisterHandler::Device() const
{
    return Dev.lock();
}
