#pragma once

#include <tuple>
#include <memory>
#include <functional> // for hash
#include <string>
#include "serial_device.h"
#include <stdint.h>

#include <wbmqtt/utils.h>

struct TTerossTMSlaveId
{
    uint32_t DeviceId;
    uint8_t CircuitId;

    TTerossTMSlaveId()
        : DeviceId(0), CircuitId(0)
    {}

    TTerossTMSlaveId(uint32_t dev_id, uint8_t circuit_id)
        : DeviceId(dev_id), CircuitId(circuit_id)
    {}

    bool operator==(const TTerossTMSlaveId &arg) const
    {
        return DeviceId == arg.DeviceId && CircuitId == arg.CircuitId;
    }
};

// specialization for std::hash
namespace std {
    template<>
    struct hash<TTerossTMSlaveId> {
        typedef TTerossTMSlaveId argument_type;
        typedef std::size_t result_type;

        result_type operator()(const argument_type& arg) const
        {
            result_type c1 = std::hash<long>{}(arg.DeviceId);
            result_type c2 = std::hash<long>{}(arg.CircuitId);

            return c1 ^ (c2 << 1);
        }
    };
};

class TTerossTMDevice;

typedef TBasicProtocol<TTerossTMDevice, TTerossTMSlaveId> TTerossTMProtocol;

class TTerossTMDevice : public TBasicProtocolSerialDevice<TTerossTMProtocol>
{
public:
    enum TRegisterType {
        REG_DEFAULT = 0,
        REG_STATE,
        REG_VERSION
    };

    TTerossTMDevice(PDeviceConfig device_config, PAbstractSerialPort port, PProtocol protocol);
    uint64_t ReadRegister(PRegister reg);
    void WriteRegister(PRegister reg, uint64_t value);

private:
    
    uint64_t ReadRegister_r(int type, int address = 0);
 
    uint16_t CalculateChecksum(const uint8_t *buffer, int size);

    size_t ReceiveReply(uint8_t *buffer, size_t max_size);
    void CheckValueReplyMessage(uint8_t *buffer, size_t size);
    void CheckVersionReplyMessage(uint8_t *buffer, size_t size);

    uint64_t ReadDataRegister(uint8_t *buf, size_t s);
    uint64_t ReadStateRegister(uint8_t *buf, size_t s);
    uint64_t ReadVersionRegister(uint8_t *buf, size_t s);

    void WriteRequest(int type, uint8_t reg);
    void WriteVersionRequest(uint8_t *buffer);
    void WriteDataRequest(uint8_t *buffer, uint8_t param_code);
    void WriteStateRequest(uint8_t *buffer);

    TMaybe<uint16_t> Version;
};
