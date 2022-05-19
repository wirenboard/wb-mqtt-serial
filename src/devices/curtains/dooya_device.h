#pragma once

#include "serial_config.h"

namespace Dooya
{
    struct TRequest
    {
        std::vector<uint8_t> Data;
        size_t ResponseSize = 0;
    };

    class TDevice: public TSerialDevice, public TUInt32SlaveId
    {
        TRequest OpenCommand;
        TRequest CloseCommand;
        TRequest GetPositionCommand;

        std::vector<uint8_t> ExecCommand(const TRequest& request);

    public:
        TDevice(PDeviceConfig config, PPort port, PProtocol protocol);

        static void Register(TSerialDeviceFactory& factory);

    protected:
        TRegisterValue ReadRegisterImpl(PRegister reg) override;
        void WriteRegisterImpl(PRegister reg, const TRegisterValue& regValue) override;
    };

    std::vector<uint8_t> MakeRequest(uint16_t address, const std::vector<uint8_t>& data);
    size_t ParsePositionResponse(uint16_t address, uint8_t fn, uint8_t dataAddress, const std::vector<uint8_t>& bytes);
}
