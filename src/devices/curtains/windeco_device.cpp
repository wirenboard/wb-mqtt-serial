#include "windeco_device.h"

namespace
{
    enum TRegTypes
    {
        POSITION,
        PARAM,
        COMMAND
    };

    const TRegisterTypes RegTypes{{POSITION, "position", "value", U8},
                                  {PARAM, "param", "value", U8, true},
                                  {COMMAND, "command", "value", U8}};

    const size_t PACKET_SIZE = 7;
    const size_t DATA_BYTE = 5;
    const size_t OPEN_COMMAND = 0x02;
    const size_t CLOSE_COMMAND = 0x04;

    uint8_t CalcCrc(const std::vector<uint8_t>& bytes, size_t size)
    {
        uint8_t res = 0;
        for (size_t i = 0; i < size; ++i) {
            res += bytes[i] * (i + 1);
        }
        return res;
    }

    void Check(uint8_t zoneId, uint8_t curtainId, const std::vector<uint8_t>& bytes)
    {
        if (bytes.size() != PACKET_SIZE) {
            throw TSerialDeviceTransientErrorException("Bad response size");
        }
        if (bytes[0] != 0x01) {
            throw TSerialDeviceTransientErrorException("Bad header");
        }
        if (bytes.back() != CalcCrc(bytes, bytes.size() - 1)) {
            throw TSerialDeviceTransientErrorException("Bad CRC");
        }
        if ((bytes[3] != zoneId) || (bytes[4] != curtainId)) {
            throw TSerialDeviceTransientErrorException("Bad address");
        }
    }

    void CheckCommandResponse(uint8_t zoneId, uint8_t curtainId, uint8_t command, const std::vector<uint8_t>& bytes)
    {
        Check(zoneId, curtainId, bytes);
        if (bytes[DATA_BYTE] != command) {
            throw TSerialDeviceTransientErrorException("Bad response command");
        }
    }

    std::vector<uint8_t> MakeSetPositionRequest(uint8_t zoneId, uint8_t curtainId, uint32_t position)
    {
        return {WinDeco::MakeRequest(zoneId, curtainId, position | 0x80)};
    }
}

void WinDeco::TDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(new TUint32SlaveIdProtocol("windeco", RegTypes),
                             new TBasicDeviceFactory<WinDeco::TDevice>("#/definitions/simple_device_no_channels"));
}

WinDeco::TDevice::TDevice(PDeviceConfig config, PProtocol protocol)
    : TSerialDevice(config, protocol),
      TUInt32SlaveId(config->SlaveId),
      ZoneId(1),
      CurtainId(SlaveId & 0xFF),
      OpenCommand{MakeRequest(ZoneId, CurtainId, OPEN_COMMAND)},
      CloseCommand{MakeRequest(ZoneId, CurtainId, CLOSE_COMMAND)},
      GetPositionCommand{MakeRequest(ZoneId, CurtainId, 0x06)},
      GetStateCommand{MakeRequest(ZoneId, CurtainId, 0x07)}
{
    if (CurtainId == 0) {
        throw TSerialDeviceException("Curtain id can't be 0");
    }
}

std::vector<uint8_t> WinDeco::TDevice::ExecCommand(TPort& port, const std::vector<uint8_t>& request)
{
    port.SleepSinceLastInteraction(GetFrameTimeout(port));
    port.WriteBytes(request);
    std::vector<uint8_t> respBytes(PACKET_SIZE);
    auto bytesRead =
        port.ReadFrame(respBytes.data(), PACKET_SIZE, GetResponseTimeout(port), GetFrameTimeout(port)).Count;
    respBytes.resize(bytesRead);
    return respBytes;
}

void WinDeco::TDevice::WriteRegisterImpl(TPort& port, const TRegisterConfig& reg, const TRegisterValue& regValue)
{
    auto value = regValue.Get<uint64_t>();
    if (reg.Type == POSITION) {
        if (value == 0) {
            CheckCommandResponse(ZoneId, CurtainId, CLOSE_COMMAND, ExecCommand(port, CloseCommand));
        } else if (value == 100) {
            CheckCommandResponse(ZoneId, CurtainId, OPEN_COMMAND, ExecCommand(port, OpenCommand));
        } else {
            ParsePositionResponse(ZoneId,
                                  CurtainId,
                                  ExecCommand(port, MakeSetPositionRequest(ZoneId, CurtainId, value)));
        }
        return;
    }
    if (reg.Type == COMMAND) {
        uint8_t addr = GetUint32RegisterAddress(reg.GetWriteAddress());
        CheckCommandResponse(ZoneId, CurtainId, addr, ExecCommand(port, MakeRequest(ZoneId, CurtainId, addr)));
        return;
    }
    throw TSerialDevicePermanentRegisterException("Unsupported register type");
}

TRegisterValue WinDeco::TDevice::ReadRegisterImpl(TPort& port, const TRegisterConfig& reg)
{
    switch (reg.Type) {
        case POSITION:
            return TRegisterValue{ParsePositionResponse(ZoneId, CurtainId, ExecCommand(port, GetPositionCommand))};
        case PARAM:
            return TRegisterValue{ParseStateResponse(ZoneId, CurtainId, ExecCommand(port, GetStateCommand))};
    }
    throw TSerialDevicePermanentRegisterException("Unsupported register type");
}

std::vector<uint8_t> WinDeco::MakeRequest(uint8_t zoneId, uint8_t curtainId, uint8_t command)
{
    std::vector<uint8_t> res{0x81, 0x00, 0x00, zoneId, curtainId, command, 0x00};
    res.back() = CalcCrc(res, PACKET_SIZE - 1);
    return res;
}

size_t WinDeco::ParsePositionResponse(uint8_t zoneId, uint8_t curtainId, const std::vector<uint8_t>& bytes)
{
    Check(zoneId, curtainId, bytes);
    if (bytes[DATA_BYTE] == 0xAA) {
        throw TSerialDeviceInternalErrorException("No limit setting");
    }
    if (bytes[DATA_BYTE] == 0xBB) {
        throw TSerialDeviceInternalErrorException("Curtain obstacle somewhere");
    }
    if (bytes[DATA_BYTE] > 100) {
        throw TSerialDeviceInternalErrorException("Unknown position");
    }
    return bytes[DATA_BYTE];
}

uint8_t WinDeco::ParseStateResponse(uint8_t zoneId, uint8_t curtainId, const std::vector<uint8_t>& bytes)
{
    Check(zoneId, curtainId, bytes);
    return bytes[DATA_BYTE];
}
