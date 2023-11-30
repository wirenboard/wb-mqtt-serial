#include "dauerhaft_device.h"

namespace
{
    enum TRegTypes
    {
        POSITION,
        COMMAND
    };

    const TRegisterTypes RegTypes{{POSITION, "position", "value", U8}, {COMMAND, "command", "value", U8}};

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
        SPEED_SETTINGS = 0xd9,
        HAND_CONTROL_SETTINGS = 0xd2,
        BAUD_RATE_SETTINGS = 0xda,
        MOTOR_SETTINGS = 0xd5
    };

    const size_t MOTOR_STATUS_RESPONSE_SIZE = 10;
    const size_t MOTOR_STATUS_POSITION_OFFSET = 8;

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

void Dauerhaft::TDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(new TUint32SlaveIdProtocol("dauerhaft", RegTypes),
                             new TBasicDeviceFactory<Dauerhaft::TDevice>("#/definitions/simple_device_no_channels"));
}

Dauerhaft::TDevice::TDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : TSerialDevice(config, port, protocol),
      TUInt32SlaveId(config->SlaveId)
{}

std::vector<uint8_t> Dauerhaft::TDevice::ExecCommand(const TRequest& request)
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

TRegisterValue Dauerhaft::TDevice::ReadRegisterImpl(PRegister reg)
{
    switch (reg->Type) {
        case COMMAND: {
            return TRegisterValue{1};
        }
        case POSITION: {
            TRequest req;
            req.Data = MakeRequest(MOTOR_STATUS, 0xcc);
            req.ResponseSize = MOTOR_STATUS_RESPONSE_SIZE;
            auto resp = ExecCommand(req);
            return TRegisterValue{resp[MOTOR_STATUS_POSITION_OFFSET]};
        }
    }
    throw TSerialDevicePermanentRegisterException("Unsupported register type");
}

void Dauerhaft::TDevice::WriteRegisterImpl(PRegister reg, const TRegisterValue& regValue)
{
    auto value = regValue.Get<uint64_t>();
    switch (reg->Type) {
        case COMMAND: {
            auto addr = GetUint32RegisterAddress(reg->GetAddress());
            TRequest req;
            req.Data = MakeRequest(CONTROL, addr);
            ExecCommand(req);
            return;
        }
        case POSITION: {
            TRequest req;
            req.Data = MakeRequest(SET_POSITION, value);
            ExecCommand(req);
            return;
        }
    }
    throw TSerialDevicePermanentRegisterException("Unsupported register type");
}

std::vector<uint8_t> Dauerhaft::MakeRequest(uint8_t command, uint8_t data)
{
    std::vector<uint8_t> res{REQUEST, 0x00, 0x00, 0x00, command, data, 0x00};
    res.back() = CalcCrc({res.begin() + 1, res.end() - 1});
    return res;
}
