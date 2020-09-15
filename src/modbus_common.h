#pragma once

#include "port.h"
#include "serial_config.h"
#include "serial_device.h"
#include "register.h"
#include <ostream>
#include <bitset>
#include <array>


namespace Modbus  // modbus protocol common utilities
{
    enum RegisterType {
        REG_HOLDING = 0, // used for 'setup' regsb
        REG_INPUT,
        REG_COIL,
        REG_DISCRETE,
        REG_HOLDING_SINGLE,
        REG_HOLDING_MULTI,
    };

    enum Error: uint8_t {
        ERR_NONE                                    = 0x0,
        ERR_ILLEGAL_FUNCTION                        = 0x1,
        ERR_ILLEGAL_DATA_ADDRESS                    = 0x2,
        ERR_ILLEGAL_DATA_VALUE                      = 0x3,
        ERR_SERVER_DEVICE_FAILURE                   = 0x4,
        ERR_ACKNOWLEDGE                             = 0x5,
        ERR_SERVER_DEVICE_BUSY                      = 0x6,
        ERR_MEMORY_PARITY_ERROR                     = 0x8,
        ERR_GATEWAY_PATH_UNAVAILABLE                = 0xA,
        ERR_GATEWAY_TARGET_DEVICE_FAILED_TO_RESPOND = 0xB
    };

    std::list<PRegisterRange> SplitRegisterList(const std::list<PRegister> & reg_list, PDeviceConfig deviceConfig, bool enableHoles);
};  // modbus protocol common utilities

class TModbusException: public TSerialDeviceTransientErrorException
{
    Modbus::Error ErrorCode;
public:
    TModbusException(Modbus::Error errorCode, const std::string& message);

    Modbus::Error code() const;
};

namespace ModbusRTU // modbus rtu protocol utilities
{
    void WriteRegister(PPort port, uint8_t slaveId, PRegister reg, uint64_t value, int shift = 0);

    void ReadRegisterRange(PPort port, uint8_t slaveId, PRegisterRange range, int shift = 0);

    void SetReadError(PRegisterRange range);

    bool WriteSetupRegisters(PPort port, uint8_t slaveId, const std::vector<PDeviceSetupItem>& setupItems, int shift = 0);
};  // modbus rtu protocol utilities
