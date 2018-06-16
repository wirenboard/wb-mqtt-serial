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
    void Read(const TIRDeviceQuery &) override;

protected:
    bool ConnectionSetup();
    ErrorType CheckForException(uint8_t* frame, int len, const char** message);

private:
    void ReadValueArray(const TIRDeviceQuery &);
    void ReadParam(const TIRDeviceQuery &);
};

typedef std::shared_ptr<TMercury230Device> PMercury230Device;
