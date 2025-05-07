#pragma once

#include "serial_config.h"

namespace Aok
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

        std::unordered_map<uint16_t, TRegisterValue> DataCache;

        TRegisterValue GetCachedResponse(uint8_t command, uint8_t data, size_t bitOffset, size_t bitWidth);

        std::vector<uint8_t> ExecCommand(const TRequest& request);

    public:
        TDevice(PDeviceConfig config, PPort port, PProtocol protocol);

        static void Register(TSerialDeviceFactory& factory);

    protected:
        TRegisterValue ReadRegisterImpl(const TRegisterConfig& reg) override;
        void WriteRegisterImpl(const TRegisterConfig& reg, const TRegisterValue& regValue) override;
        void InvalidateReadCache() override;
    };

    std::vector<uint8_t> MakeRequest(uint8_t id,
                                     uint8_t channelLow,
                                     uint8_t channelHigh,
                                     uint8_t command,
                                     uint8_t data);
}
