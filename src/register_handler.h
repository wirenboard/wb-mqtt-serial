#pragma once

#include "register.h"

class TRegisterHandler
{
public:
    TRegisterHandler(PRegister reg, const std::string& value);

    PRegister Register() const;

    /**
     * @brief Write pending register value.
     *
     * @return true - Successful write
     * @return false - Write error
     */
    bool Flush();

    void SetValue(const std::string& v);
    PSerialDevice Device() const;

private:
    uint64_t ValueToSet = 0;
    PRegister Reg;
    bool WriteFail;
    std::chrono::steady_clock::time_point WriteFirstTryTime;
};

typedef std::shared_ptr<TRegisterHandler> PRegisterHandler;
