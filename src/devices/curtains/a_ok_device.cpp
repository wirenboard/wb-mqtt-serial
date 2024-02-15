#include "a_ok_device.h"

namespace
{
    enum TRegTypes
    {
        POSITION,
        COMMAND,
        PARAM
    };

    const TRegisterTypes RegTypes{{POSITION, "position", "value", U8},
                                  {COMMAND, "command", "value", U8},
                                  {PARAM, "param", "value", U8}};

    enum THeadCodes
    {
        REQUEST = 0x9a,
        RESPONSE = 0xd8
    };

    enum TCommandTypes
    {
        CONTROL = 0x0a,
        SET_POSITION = 0xdd,
        MOTOR_STATUS = 0xcc,
        CURTAIN_MOTOR_STATUS = 0xca,
        HAND_CONTROL_SETTINGS = 0xd2,
        MOTOR_SETTINGS = 0xd5
    };

    const size_t MOTOR_STATUS_RESPONSE_SIZE = 10;
    // const size_t CURTAIN_MOTOR_STATUS_RESPONSE_SIZE = 9;
    const size_t MOTOR_STATUS_POSITION_OFFSET = 7;
    // const size_t CURTAIN_MOTOR_STATUS_BITS_OFFSET = 8;

    uint8_t CalcCrc(const std::vector<uint8_t>& bytes)
    {
        uint8_t xorResult = 0;
        for (uint8_t value: bytes) {
            xorResult ^= value;
        }
        return xorResult;
    }

    TPort::TFrameCompletePred ExpectNBytes(size_t n)
    {
        return [=](uint8_t* buf, size_t size) {
            if (size < n)
                return false;
            // Got expected bytes count, head code and CRC is ok
            if (size == n && buf[0] == RESPONSE && buf[n - 1] == CalcCrc({buf + 1, buf + n - 1})) {
                return true;
            }
            // Malformed packet or packet with error code. Wait for frame timeout
            return false;
        };
    }
}

void Aok::TDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(new TUint32SlaveIdProtocol("a_ok", RegTypes),
                             new TBasicDeviceFactory<Aok::TDevice>("#/definitions/simple_device_no_channels"));
}

Aok::TDevice::TDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : TSerialDevice(config, port, protocol),
      TUInt32SlaveId(config->SlaveId),
      MotorId((SlaveId >> 16) & 0xFF),
      LowChannelId((SlaveId >> 8) & 0xFF),
      HighChannelId(SlaveId & 0xFF)
{}

std::vector<uint8_t> Aok::TDevice::ExecCommand(const TRequest& request)
{
    Port()->SleepSinceLastInteraction(DeviceConfig()->FrameTimeout);
    Port()->SkipNoise();
    Port()->WriteBytes(request.Data);
    std::vector<uint8_t> respBytes(request.ResponseSize);
    if (request.ResponseSize != 0) {
        auto bytesRead = Port()
                             ->ReadFrame(respBytes.data(),
                                         respBytes.size(),
                                         DeviceConfig()->ResponseTimeout,
                                         DeviceConfig()->FrameTimeout,
                                         ExpectNBytes(request.ResponseSize))
                             .Count;
        respBytes.resize(bytesRead);
    }
    return respBytes;
}

TRegisterValue Aok::TDevice::ReadRegisterImpl(PRegister reg)
{
    switch (reg->Type) {
        case COMMAND: {
            return TRegisterValue{1};
        }
        case POSITION: {
            TRequest req;
            req.Data = MakeRequest(MotorId, LowChannelId, HighChannelId, MOTOR_STATUS, 0xcc);
            req.ResponseSize = MOTOR_STATUS_RESPONSE_SIZE;
            auto resp = ExecCommand(req);
            return TRegisterValue{resp[MOTOR_STATUS_POSITION_OFFSET]};
        }
    }
    throw TSerialDevicePermanentRegisterException("Unsupported register type");
}

void Aok::TDevice::WriteRegisterImpl(PRegister reg, const TRegisterValue& regValue)
{
    auto value = regValue.Get<uint64_t>();
    switch (reg->Type) {
        case COMMAND: {
            auto addr = GetUint32RegisterAddress(reg->GetAddress());
            TRequest req;
            req.Data = MakeRequest(MotorId, LowChannelId, HighChannelId, CONTROL, addr);
            ExecCommand(req);
            return;
        }
        case POSITION: {
            TRequest req;
            req.Data = MakeRequest(MotorId, LowChannelId, HighChannelId, SET_POSITION, value);
            ExecCommand(req);
            return;
        }
        case PARAM: {
            auto addr = GetUint32RegisterAddress(reg->GetAddress());
            TRequest req;
            req.Data = MakeRequest(MotorId, LowChannelId, HighChannelId, addr, value);
            ExecCommand(req);
            return;
        }
    }
    throw TSerialDevicePermanentRegisterException("Unsupported register type");
}

std::vector<uint8_t> Aok::MakeRequest(uint8_t id,
                                      uint8_t channelLow,
                                      uint8_t channelHigh,
                                      uint8_t command,
                                      uint8_t data)
{
    std::vector<uint8_t> res{REQUEST, id, channelLow, channelHigh, command, data, 0x00};
    res.back() = CalcCrc({res.begin() + 1, res.end() - 1});
    return res;
}
