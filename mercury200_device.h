#pragma once

#include <cstdint>
#include <cstddef>

#include <list>
#include <array>
#include <string>
#include <memory>
#include <exception>
#include <unordered_map>

#include "serial_device.h"

class TMercury200Device : public TBasicProtocolSerialDevice<TBasicProtocol<TMercury200Device>>
{
public:
    enum RegisterType
    {
        REG_PARAM_VALUE8 = 0,
        REG_PARAM_VALUE16 = 1,
        REG_PARAM_VALUE24 = 2,
        REG_PARAM_VALUE32 = 3,
    };

    TMercury200Device(PDeviceConfig config, PPort port, PProtocol protocol);
    ~TMercury200Device();
    uint64_t ReadMemoryBlock(const PMemoryBlock & mb) override;
    void WriteMemoryBlock(const PMemoryBlock & mb, uint64_t value) override;
    void EndPollCycle() override;

private:
    std::vector<uint8_t> ExecCommand(uint8_t cmd);
    // buf expected to be 7 bytes long
    void FillCommand(uint8_t* buf, uint32_t id, uint8_t cmd) const;
    int RequestResponse(uint32_t slave, uint8_t cmd, uint8_t* response) const;
    bool IsBadHeader(uint32_t slave_expected, uint8_t cmd_expected, uint8_t *response) const;

    bool IsCrcValid(uint8_t *buf, int sz) const;

    std::unordered_map<uint8_t, std::vector<uint8_t> > CmdResultCache;
};

typedef std::shared_ptr<TMercury200Device> PMercury200Device;
