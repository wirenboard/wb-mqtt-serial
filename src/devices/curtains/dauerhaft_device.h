#pragma once

#include "serial_config.h"

namespace Dauerhaft
{
    struct TRequest
    {
        std::vector<uint8_t> Data;
        size_t ResponseSize = 0;
    };
    class TDevice: public TSerialDevice, public TUInt32SlaveId
    {
        uint8_t MotorId;
        uint8_t LowChannelId;
        uint8_t HighChannelId;

        std::vector<uint8_t> ExecCommand(const TRequest& request);

    public:
        TDevice(PDeviceConfig config, PPort port, PProtocol protocol);

        static void Register(TSerialDeviceFactory& factory);

    protected:
        TRegisterValue ReadRegisterImpl(PRegister reg) override;
        void WriteRegisterImpl(PRegister reg, const TRegisterValue& regValue) override;
    };

    std::vector<uint8_t> MakeRequest(uint8_t id,
                                     uint8_t channelLow,
                                     uint8_t channelHigh,
                                     uint8_t command,
                                     uint8_t data);
}
