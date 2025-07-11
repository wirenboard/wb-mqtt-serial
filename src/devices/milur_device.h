#pragma once

#include <cstdint>
#include <exception>
#include <memory>
#include <string>

#include "em_device.h"
#include "serial_config.h"

class TMilurDevice: public TEMDevice
{
public:
    enum RegisterType
    {
        REG_PARAM = 0,
        REG_POWER,
        REG_ENERGY,
        REG_FREQ,
        REG_POWERFACTOR
    };

    TMilurDevice(PDeviceConfig device_config, PProtocol protocol);

    static void Register(TSerialDeviceFactory& factory);

    TRegisterValue ReadRegisterImpl(TPort& port, const TRegisterConfig& reg) override;

    std::chrono::milliseconds GetFrameTimeout(TPort& port) const override;

protected:
    void PrepareImpl(TPort& port) override;
    bool ConnectionSetup(TPort& port) override;
    ErrorType CheckForException(uint8_t* frame, int len, const char** message) override;
    uint64_t BuildIntVal(uint8_t* p, int sz) const;
    uint64_t BuildBCB32(uint8_t* psrc) const;
    int GetExpectedSize(int type) const;
};

typedef std::shared_ptr<TMilurDevice> PMilurDevice;
