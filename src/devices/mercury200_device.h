#pragma once

#include <cstddef>
#include <cstdint>

#include <array>
#include <exception>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>

#include "serial_config.h"
#include "serial_device.h"

class TMercury200Device: public TSerialDevice, public TUInt32SlaveId
{
public:
    TMercury200Device(PDeviceConfig config, PPort port, PProtocol protocol);

    static void Register(TSerialDeviceFactory& factory);

    TRegisterValue ReadRegisterImpl(const TRegisterConfig& reg) override;
    void InvalidateReadCache() override;

private:
    std::vector<uint8_t> ExecCommand(uint8_t cmd);
    // buf expected to be 7 bytes long
    void FillCommand(uint8_t* buf, uint32_t id, uint8_t cmd) const;
    int RequestResponse(uint32_t slave, uint8_t cmd, uint8_t* response) const;
    bool IsBadHeader(uint32_t slave_expected, uint8_t cmd_expected, uint8_t* response) const;

    bool IsCrcValid(uint8_t* buf, int sz) const;

    std::unordered_map<uint8_t, std::vector<uint8_t>> CmdResultCache;
};

typedef std::shared_ptr<TMercury200Device> PMercury200Device;
