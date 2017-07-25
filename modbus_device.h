#pragma once

#include <string>
#include <memory>
#include <exception>
#include <stdint.h>

#include "serial_device.h"

class TModbusDevice : public TBasicProtocolSerialDevice<TBasicProtocol<TModbusDevice>> {
    using TRTUReadRequest = std::array<uint8_t, 8>;
    using TRTUWriteRequest = std::vector<uint8_t>;
public:
    static const int DefaultTimeoutMs = 1000;
    enum RegisterType {
        REG_HOLDING = 0, // used for 'setup' regsb
        REG_INPUT,
        REG_COIL,
        REG_DISCRETE,
    };

    TModbusDevice(PDeviceConfig config, PAbstractSerialPort port, PProtocol protocol);
    virtual std::list<PRegisterRange> SplitRegisterList(const std::list<PRegister> reg_list) const;
    uint64_t ReadRegister(PRegister reg);
    void WriteRegister(PRegister reg, uint64_t value);
    void ReadRegisterRange(PRegisterRange range);

private:
    void ComposeRTUReadRequest(TRTUReadRequest& req, PRegisterRange range);
    void ComposeRTUWriteRequest(TRTUWriteRequest& req, PRegister reg, uint64_t value);

    std::chrono::microseconds FrameTimeout;
};
