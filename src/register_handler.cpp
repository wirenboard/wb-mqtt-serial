#include "register_handler.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[register handler] "

using namespace std::chrono;

TRegisterHandler::TRegisterHandler(PRegister reg): Reg(reg), WriteFail(false)
{}

bool TRegisterHandler::NeedToFlush()
{
    std::lock_guard<std::mutex> lock(SetValueMutex);
    return Dirty;
}

void TRegisterHandler::HandleWriteErrorNoRetry(TPort& port, const TRegisterValue& tempValue, const char* msg)
{
    LOG(Warn) << port.GetDescription() << " " << Reg->Device()->ToString() << ": failed to write <"
              << Reg->GetConfig()->ToString() << ">: " << msg;
    {
        std::lock_guard<std::mutex> lock(SetValueMutex);
        Dirty = (tempValue != ValueToSet);
        WriteFail = false;
    }
    Reg->SetError(TRegister::TError::WriteError);
}

void TRegisterHandler::HandleWriteErrorRetryWrite(TPort& port, const TRegisterValue& tempValue, const char* msg)
{
    LOG(Warn) << port.GetDescription() << " " << Reg->Device()->ToString() << ": failed to write <"
              << Reg->GetConfig()->ToString() << ">: " << msg;
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

void TRegisterHandler::Flush(TPort& port)
{
    TRegisterValue tempValue; // must be volatile
    try {
        {
            std::lock_guard<std::mutex> lock(SetValueMutex);
            tempValue = ValueToSet;
        }
        Reg->Device()->WriteRegister(port, Reg, tempValue);
        {
            std::lock_guard<std::mutex> lock(SetValueMutex);
            Dirty = (tempValue != ValueToSet);
            WriteFail = false;
        }
        Reg->SetValue(tempValue, false);
        Reg->ClearError(TRegister::TError::WriteError);
    } catch (const TSerialDeviceInternalErrorException& e) {
        HandleWriteErrorNoRetry(port, tempValue, e.what());
        return;
    } catch (const TSerialDevicePermanentRegisterException& e) {
        HandleWriteErrorNoRetry(port, tempValue, e.what());
        return;
    } catch (const TSerialDeviceException& e) {
        HandleWriteErrorRetryWrite(port, tempValue, e.what());
    }
}

bool TRegisterHandler::NeedRetryAfterWriteFail()
{
    std::lock_guard<std::mutex> lock(SetValueMutex);
    if (!Dirty) {
        return false;
    }
    if (!WriteFail) {
        WriteFirstTryTime = steady_clock::now();
        WriteFail = true;
    }
    if (duration_cast<seconds>(steady_clock::now() - WriteFirstTryTime) >
        Reg->Device()->DeviceConfig()->MaxWriteFailTime)
    {
        Dirty = false;
        WriteFail = false;
        return false;
    }
    return true;
}

void TRegisterHandler::SetTextValue(const std::string& v)
{
    std::lock_guard<std::mutex> lock(SetValueMutex);
    Dirty = true;
    ValueToSet = ConvertToRawValue(*Reg->GetConfig(), v);
}

PRegister TRegisterHandler::Register() const
{
    return Reg;
}
