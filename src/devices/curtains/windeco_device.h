#pragma once

#include "serial_config.h"

namespace WinDeco
{
    class TDevice : public TSerialDevice, public TUInt32SlaveId
    {
        uint8_t ZoneId;
        uint8_t CurtainId;

        std::vector<uint8_t> OpenCommand;
        std::vector<uint8_t> CloseCommand;
        std::vector<uint8_t> GetPositionCommand;
        std::vector<uint8_t> GetStateCommand;

        std::vector<uint8_t> ExecCommand(const std::vector<uint8_t>& request);

    public:
        TDevice(PDeviceConfig config, PPort port, PProtocol protocol);

        void WriteRegister(PRegister reg, uint64_t value);
        uint64_t ReadRegister(PRegister reg);

        static void Register(TSerialDeviceFactory& factory);
    };

    std::vector<uint8_t> MakeRequest(uint8_t zoneId, uint8_t curtainId, uint8_t command);
    size_t               ParsePositionResponse(uint8_t zoneId, uint8_t curtainId, const std::vector<uint8_t>& bytes);
    uint8_t              ParseStateResponse(uint8_t zoneId, uint8_t curtainId, const std::vector<uint8_t>& bytes);
}
