#include "register_handler.h"
#include "log.h"
#include "serial_device.h"

#define LOG(logger) ::logger.Log() << "[register handler] "

using namespace std::chrono;

namespace
{
    const seconds MAX_WRITE_FAIL_TIME(600); // 10 minutes
}

TRegisterHandler::TRegisterHandler(PRegister reg, const std::string& value): Reg(reg), WriteFail(false)
{
    SetValue(value);
}

bool TRegisterHandler::Flush()
{
    try {
        Device()->WriteRegister(Reg, ValueToSet);
        Reg->SetValue(ValueToSet, false);
        Reg->ClearError(TRegister::TError::WriteError);
    } catch (const TSerialDevicePermanentRegisterException& e) {
        LOG(Warn) << "failed to write: " << Reg->ToString() << ": " << e.what();
        Reg->SetError(TRegister::TError::WriteError);
    } catch (const TSerialDeviceException& e) {
        LOG(Warn) << "failed to write: " << Reg->ToString() << ": " << e.what();
        if (!WriteFail) {
            WriteFirstTryTime = steady_clock::now();
        }
        if (duration_cast<seconds>(steady_clock::now() - WriteFirstTryTime) <= MAX_WRITE_FAIL_TIME) {
            Reg->SetError(TRegister::TError::WriteError);
            WriteFail = true;
            return false;
        }
        WriteFail = false;
    }
    return true;
}

void TRegisterHandler::SetValue(const std::string& v)
{
    ValueToSet = ConvertToRawValue(*Reg, v);
    WriteFail = false;
}

PRegister TRegisterHandler::Register() const
{
    return Reg;
}

PSerialDevice TRegisterHandler::Device() const
{
    return Reg->Device();
}
