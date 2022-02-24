#pragma once
#include "bcd_utils.h"
#include "binary_semaphore.h"
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
    TRegisterHandler(PSerialDevice dev, PRegister reg);

    PRegister Register() const;

    bool NeedToFlush();

    /**
     * @brief Write pending register value. NeedToFlush must be checked before call.
     */
    void Flush();

    void SetTextValue(const std::string& v);
    PSerialDevice Device() const;

private:
    std::weak_ptr<TSerialDevice> Dev;
    uint64_t ValueToSet = 0;
    PRegister Reg;
    volatile bool Dirty = false;
    std::mutex SetValueMutex;
    bool WriteFail;
    std::chrono::steady_clock::time_point WriteFirstTryTime;
};

typedef std::shared_ptr<TRegisterHandler> PRegisterHandler;
