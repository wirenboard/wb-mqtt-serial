#pragma once
#include "bcd_utils.h"
#include "register.h"
#include "serial_device.h"
#include <cmath>
#include <memory>
#include <mutex>
#include <string>
#include <wblib/utils.h>

using WBMQTT::StringFormat;

class TRegisterHandler
{
public:
    TRegisterHandler(PRegister reg);

    PRegister Register() const;

    bool NeedToFlush();

    /**
     * @brief Write pending register value. NeedToFlush must be checked before call.
     */
    void Flush();

    void SetTextValue(const std::string& v);

private:
    TRegisterValue ValueToSet{0};
    PRegister Reg;
    volatile bool Dirty = false;
    std::mutex SetValueMutex;
    bool WriteFail;
    std::chrono::steady_clock::time_point WriteFirstTryTime;

    void HandleWriteErrorNoRetry(const TRegisterValue& tempValue, const char* msg);
    void HandleWriteErrorRetryWrite(const TRegisterValue& tempValue, const char* msg);
};

typedef std::shared_ptr<TRegisterHandler> PRegisterHandler;
