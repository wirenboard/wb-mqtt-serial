#pragma once

#include <list>
#include <vector>
#include <string>
#include <memory>
#include <exception>
#include <unordered_map>
#include <cstdint>

#include "em_device.h"

class TMercury230Device;
typedef TBasicProtocol<TMercury230Device> TMercury230Protocol;

class TMercury230Device: public TEMDevice<TBasicProtocol<TMercury230Device>> {
public:
    static const int DefaultTimeoutMs = 1000;
    enum RegisterType {
        REG_VALUE_ARRAY = 0,
        REG_PARAM = 1,
        REG_PARAM_SIGN_ACT = 2,
        REG_PARAM_SIGN_REACT = 3,
        REG_PARAM_SIGN_IGNORE = 4,
        REG_PARAM_BE = 5,
        REG_VALUE_ARRAY12 = 6
    };

    TMercury230Device(PDeviceConfig, PPort port, PProtocol protocol);
    std::vector<uint8_t> ReadProtocolRegister(const PProtocolRegister & reg) override;
    void EndPollCycle() override;

protected:
    bool ConnectionSetup();
    ErrorType CheckForException(uint8_t* frame, int len, const char** message);
    const TIRDeviceMemoryView & CreateMemoryView(const std::vector<uint8_t> & memory, const PProtocolRegister & memoryBlock) override;

private:
    struct TValueArray {
        uint32_t values[4];
    };
    std::vector<uint8_t> ReadValueArray(const PProtocolRegister & mb);
    std::vector<uint8_t> ReadParam(const PProtocolRegister & mb);

    std::unordered_map<int, TValueArray> CachedValues;
};

typedef std::shared_ptr<TMercury230Device> PMercury230Device;
