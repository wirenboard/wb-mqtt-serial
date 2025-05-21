#include "dooya_device.h"
#include "bin_utils.h"
#include "crc16.h"

using namespace BinUtils;

namespace
{
    enum TRegTypes
    {
        POSITION,
        PARAM,
        COMMAND,
        ANGLE,
        MOTOR_TYPE,
        MOTOR_SITUATION
    };

    const TRegisterTypes RegTypes{{POSITION, "position", "value", U8},
                                  {PARAM, "param", "value", U8},
                                  {COMMAND, "command", "value", U8},
                                  {ANGLE, "angle", "value", U8},
                                  {MOTOR_TYPE, "type", "text", String, true},
                                  {MOTOR_SITUATION, "status", "text", String, true}};

    enum TCommands
    {
        READ = 1,
        WRITE = 2,
        CONTROL = 3
    };

    enum TControlCommandDataAddresses
    {
        OPEN = 1,
        CLOSE = 2,
        SET_POSITION = 4
    };

    enum TReadCommandDataAddresses
    {
        GET_POSITION = 2
    };

    const uint8_t GET_POSITION_DATA_LENGTH = 1;

    const size_t CRC_SIZE = sizeof(uint16_t);
    const size_t CONTROL_RESPONSE_SIZE = 7;
    const size_t RESPONSE_SIZE = 8;
    const size_t DATA_POSITION = 5;

    const size_t ADDRESS_POSITION = 1;
    const size_t ADDRESS_SIZE = sizeof(uint16_t);

    Dooya::TRequest MakeSetPositionRequest(uint16_t address, uint8_t position)
    {
        return {Dooya::MakeRequest(address, {CONTROL, SET_POSITION, position}), RESPONSE_SIZE};
    }

    Dooya::TRequest MakeSetAngleRequest(uint16_t address, uint8_t angle)
    {
        return {Dooya::MakeRequest(address, {CONTROL, SET_POSITION, 0xFF, angle}), RESPONSE_SIZE + 1};
    }

    uint16_t CalcCrc(const uint8_t* data, size_t size)
    {
        auto crc = CRC16::CalculateCRC16(data, size);
        return ((crc >> 8) & 0xFF) | ((crc << 8) & 0xFF00);
    }

    void Check(uint16_t address, size_t size, uint8_t fn, uint8_t dataAddress, const std::vector<uint8_t>& bytes)
    {
        if (bytes.size() < size) {
            throw TSerialDeviceTransientErrorException("Bad response size");
        }
        if (bytes[0] != 0x55) {
            throw TSerialDeviceTransientErrorException("Bad header");
        }
        if (Get<uint16_t>(bytes.cend() - CRC_SIZE, bytes.cend()) != CalcCrc(&bytes[0], bytes.size() - CRC_SIZE)) {
            throw TSerialDeviceTransientErrorException("Bad CRC");
        }
        if (Get<uint16_t>(bytes.cbegin() + ADDRESS_POSITION, bytes.cbegin() + ADDRESS_POSITION + ADDRESS_SIZE) !=
            address)
        {
            throw TSerialDeviceTransientErrorException("Bad address");
        }
        if ((bytes[3] != fn) || (bytes[4] != dataAddress)) {
            throw TSerialDeviceTransientErrorException("Bad response");
        }
    }

    uint8_t ParseReadResponse(uint16_t address, uint8_t fn, uint8_t dataAddress, const std::vector<uint8_t>& bytes)
    {
        Check(address, RESPONSE_SIZE, fn, dataAddress, bytes);
        return bytes[DATA_POSITION];
    }

    TPort::TFrameCompletePred ExpectNBytes(size_t n)
    {
        return [=](uint8_t* buf, size_t size) {
            if (size < n)
                return false;
            // Got expected bytes count and CRC is ok
            if (size == n && Get<uint16_t>(buf + n - CRC_SIZE, buf + n) == CalcCrc(buf, n - CRC_SIZE)) {
                return true;
            }
            // Malformed packet or packet with error code. Wait for frame timeout
            return false;
        };
    }
}

void Dooya::TDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(new TUint32SlaveIdProtocol("dooya", RegTypes),
                             new TBasicDeviceFactory<Dooya::TDevice>("#/definitions/simple_device_no_channels"));
}

Dooya::TDevice::TDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : TSerialDevice(config, port, protocol),
      TUInt32SlaveId(config->SlaveId),
      OpenCommand{MakeRequest(SlaveId, {CONTROL, OPEN}), CONTROL_RESPONSE_SIZE},
      CloseCommand{MakeRequest(SlaveId, {CONTROL, CLOSE}), CONTROL_RESPONSE_SIZE},
      GetPositionCommand{MakeRequest(SlaveId, {READ, GET_POSITION, GET_POSITION_DATA_LENGTH}), RESPONSE_SIZE}
{
    config->FrameTimeout =
        std::max(config->FrameTimeout, std::chrono::ceil<std::chrono::milliseconds>(port->GetSendTimeBytes(3.5)));
}

std::vector<uint8_t> Dooya::TDevice::ExecCommand(const TRequest& request)
{
    Port()->WriteBytes(request.Data);
    // Reserve extra space for packets with errors
    // Example: Open command good response, 7 bytes: 55 01 01 03 01 b9 00
    //          Open error (could be already opened), 8 bytes: 55 01 01 03 01 ff 81 f2
    std::vector<uint8_t> respBytes(request.ResponseSize + 10);
    auto bytesRead = Port()
                         ->ReadFrame(respBytes.data(),
                                     respBytes.size(),
                                     DeviceConfig()->ResponseTimeout,
                                     DeviceConfig()->FrameTimeout,
                                     ExpectNBytes(request.ResponseSize))
                         .Count;
    respBytes.resize(bytesRead);
    return respBytes;
}

void Dooya::TDevice::WriteRegisterImpl(const TRegisterConfig& reg, const TRegisterValue& regValue)
{
    auto value = regValue.Get<uint64_t>();
    switch (reg.Type) {
        case POSITION: {
            if (value == 0) {
                if (CloseCommand.Data != ExecCommand(CloseCommand)) {
                    throw TSerialDeviceTransientErrorException("Bad response");
                }
            } else if (value == 100) {
                if (OpenCommand.Data != ExecCommand(OpenCommand)) {
                    throw TSerialDeviceTransientErrorException("Bad response");
                }
            } else {
                ParsePositionResponse(SlaveId,
                                      CONTROL,
                                      SET_POSITION,
                                      ExecCommand(MakeSetPositionRequest(SlaveId, value)));
            }
            return;
        }
        case PARAM: {
            uint8_t dataAddress = GetUint32RegisterAddress(reg.GetWriteAddress());
            TRequest req;
            req.Data = MakeRequest(SlaveId, {WRITE, dataAddress, 1, static_cast<uint8_t>(value)});
            req.ResponseSize = RESPONSE_SIZE;
            if (ParseReadResponse(SlaveId, WRITE, dataAddress, ExecCommand(req)) != 1) {
                throw TSerialDeviceTransientErrorException("Bad response");
            }
            return;
        }
        case COMMAND: {
            auto data = GetUint32RegisterAddress(reg.GetWriteAddress());
            TRequest req;
            uint8_t dataAddress;
            // Command with parameter
            if (data > 0xFF) {
                dataAddress = static_cast<uint8_t>((data >> 8) & 0xFF);
                req.Data = MakeRequest(SlaveId, {CONTROL, dataAddress, static_cast<uint8_t>(data & 0xFF)});
                req.ResponseSize = CONTROL_RESPONSE_SIZE + 1;
            } else {
                dataAddress = static_cast<uint8_t>(data & 0xFF);
                req.Data = MakeRequest(SlaveId, {CONTROL, dataAddress});
                req.ResponseSize = CONTROL_RESPONSE_SIZE;
            }
            ParseCommandResponse(SlaveId, CONTROL, dataAddress, req, ExecCommand(req));
            return;
        }
        case ANGLE: {
            ExecCommand(MakeSetAngleRequest(SlaveId, value));
            return;
        }
    }
}

TRegisterValue Dooya::TDevice::ReadEnumParameter(const TRegisterConfig& reg,
                                                 const std::unordered_map<uint8_t, std::string>& names)
{
    auto addr = GetUint32RegisterAddress(reg.GetAddress());
    TRequest req;
    req.Data = MakeRequest(SlaveId, {READ, static_cast<uint8_t>(addr & 0xFF), 1});
    req.ResponseSize = RESPONSE_SIZE;
    uint8_t res = ParseReadResponse(SlaveId, READ, 1, ExecCommand(req));
    auto it = names.find(res);
    if (it != names.end()) {
        return TRegisterValue{it->second};
    }
    return TRegisterValue{"0x" + WBMQTT::HexDump(&res, 1)};
}

TRegisterValue Dooya::TDevice::ReadRegisterImpl(const TRegisterConfig& reg)
{
    switch (reg.Type) {
        case POSITION: {
            return TRegisterValue{
                ParsePositionResponse(SlaveId, READ, GET_POSITION_DATA_LENGTH, ExecCommand(GetPositionCommand))};
        }
        case PARAM: {
            auto addr = GetUint32RegisterAddress(reg.GetAddress());
            TRequest req;
            req.Data = MakeRequest(SlaveId, {READ, static_cast<uint8_t>(addr & 0xFF), 1});
            req.ResponseSize = RESPONSE_SIZE;
            return TRegisterValue{ParseReadResponse(SlaveId, READ, 1, ExecCommand(req))};
        }
        case ANGLE: {
            auto addr = 0x06;
            TRequest req;
            req.Data = MakeRequest(SlaveId, {READ, static_cast<uint8_t>(addr & 0xFF), 1});
            req.ResponseSize = RESPONSE_SIZE;
            return TRegisterValue{ParseReadResponse(SlaveId, READ, 1, ExecCommand(req))};
        }
        case MOTOR_TYPE: {
            std::unordered_map<uint8_t, std::string> names{{0x11, "roller"}, {0x12, "venetian"}};
            return ReadEnumParameter(reg, names);
        }
        case MOTOR_SITUATION: {
            std::unordered_map<uint8_t, std::string> names{{0, "stopped"},
                                                           {1, "opening"},
                                                           {2, "closing"},
                                                           {3, "setting"}};
            return ReadEnumParameter(reg, names);
        }
    }
    throw TSerialDevicePermanentRegisterException("Unsupported register type");
}

std::vector<uint8_t> Dooya::MakeRequest(uint16_t address, const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> res{0x55};
    auto it = std::back_inserter(res);
    Append(it, address);
    std::copy(data.begin(), data.end(), it);
    Append(it, CalcCrc(res.data(), res.size()));
    return res;
}

size_t Dooya::ParsePositionResponse(uint16_t address,
                                    uint8_t fn,
                                    uint8_t dataAddress,
                                    const std::vector<uint8_t>& bytes)
{
    Check(address, RESPONSE_SIZE, fn, dataAddress, bytes);
    if (bytes[DATA_POSITION] == 0xFF) {
        throw TSerialDeviceInternalErrorException("No limit setting");
    }
    if (bytes[DATA_POSITION] > 100) {
        throw TSerialDeviceInternalErrorException("Unknown position");
    }
    return bytes[DATA_POSITION];
}

void Dooya::ParseCommandResponse(uint16_t address,
                                 uint8_t fn,
                                 uint8_t dataAddress,
                                 const Dooya::TRequest& request,
                                 const std::vector<uint8_t>& responseBytes)
{
    Check(address, request.ResponseSize, fn, dataAddress, responseBytes);

    // Device must return the same bytes as in command
    if (request.Data != responseBytes) {
        // Unsupported command parameter or an internal device's error
        // Example: move to unset position, no limit settings, open already opened curtain etc
        if (responseBytes[DATA_POSITION] == 0xFF) {
            throw TSerialDeviceInternalErrorException("device can't execute command");
        }
        throw TSerialDeviceInternalErrorException("response mismatch");
    }
}
