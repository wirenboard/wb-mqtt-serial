#include "register_handler.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[register handler] "

using namespace std::chrono;

TRegisterHandler::TRegisterHandler(PSerialDevice dev, PRegister reg): Dev(dev), Reg(reg), WriteFail(false)
{}

bool TRegisterHandler::NeedToFlush()
{
    std::lock_guard<std::mutex> lock(SetValueMutex);
    return Dirty;
}

void TRegisterHandler::HandleWriteErrorNoRetry(const TRegisterValue& tempValue, const char* msg)
{
    LOG(Warn) << "failed to write: " << Reg->ToString() << ": " << msg;
    {
        std::lock_guard<std::mutex> lock(SetValueMutex);
        Dirty = (tempValue != ValueToSet);
        WriteFail = false;
    }
    Reg->SetError(TRegister::TError::WriteError);
}

void TRegisterHandler::HandleWriteErrorRetryWrite(const TRegisterValue& tempValue, const char* msg)
{
    LOG(Warn) << "failed to write: " << Reg->ToString() << ": " << msg;
    {
        std::lock_guard<std::mutex> lock(SetValueMutex);
        if (!WriteFail) {
            WriteFirstTryTime = steady_clock::now();
        }
        WriteFail = true;
        if (duration_cast<seconds>(steady_clock::now() - WriteFirstTryTime) >
            Reg->Device()->DeviceConfig()->MaxWriteFailTime)
        {
            Dirty = (tempValue != ValueToSet);
            WriteFail = false;
        }
    }
    Reg->SetError(TRegister::TError::WriteError);
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
    } catch (const TSerialDeviceInternalErrorException& e) {
        HandleWriteErrorNoRetry(tempValue, e.what());
        return;
    } catch (const TSerialDevicePermanentRegisterException& e) {
        HandleWriteErrorNoRetry(tempValue, e.what());
        return;
    } catch (const TSerialDeviceException& e) {
        HandleWriteErrorRetryWrite(tempValue, e.what());
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
