#pragma once

#include "serial_config.h"

namespace Somfy
{
    enum TMsg
    {
        CTRL_MOVE = 0x01,
        CTRL_STOP = 0x02,
        CTRL_MOVETO = 0x03,
        CTRL_MOVEOF = 0x04,
        CTRL_WINK = 0x05,
        GET_MOTOR_POSITION = 0x0C,
        POST_MOTOR_POSITION = 0x0D,
        GET_MOTO_STATUS = 0x0E,
        SET_MOTOR_LIMITS = 0x11,
        NACK = 0x6F,
        ACK = 0x7F
    };

    enum TNodeType
    {
        SONESSE_30 = 2, // Sonesse 30 / Ø30 DC Serie
        RTS_TRANSMITTER = 5,
        GLYDEA = 6,
        AC_50 = 7, // Ø50 AC Serie
        DC_50 = 8, // Ø50 DC Serie
        AC_40 = 9  // Ø40 AC Serie
    };

    class TDevice: public TSerialDevice, public TUInt32SlaveId
    {
        std::vector<uint8_t> OpenCommand;
        std::vector<uint8_t> CloseCommand;
        uint8_t NodeType;

        std::unordered_map<uint8_t, uint64_t> DataCache;

        uint64_t GetCachedResponse(uint8_t requestHeader, uint8_t responseHeader, size_t bitOffset, size_t bitWidth);

        std::vector<uint8_t> ExecCommand(const std::vector<uint8_t>& request);

    public:
        TDevice(PDeviceConfig config, uint8_t nodeType, PPort port, PProtocol protocol);

        void     WriteRegister(PRegister reg, uint64_t value);
        uint64_t ReadRegister(PRegister reg);
        void     EndPollCycle();

        static void Register(TSerialDeviceFactory& factory);
    };

    std::vector<uint8_t> MakeRequest(uint8_t msg,
                                     uint32_t address,
                                     uint8_t nodeType,
                                     const std::vector<uint8_t>& data = std::vector<uint8_t>());
    std::vector<uint8_t> MakeSetPositionRequest(uint32_t address, uint8_t nodeType, uint32_t position);

    uint64_t ParseStatusReport(uint32_t address, uint8_t header, const std::vector<uint8_t>& bytes);

    //! All bytes except CRC in transmitted packet are inverted, let's invert them back to get real packet
    void FixReceivedFrame(std::vector<uint8_t>& bytes);
}
