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
        COMMAND
    };

    const TRegisterTypes RegTypes{
        {POSITION, "position", "value", U8},
        {PARAM, "param", "value", U8},
        {COMMAND, "command", "value", U8},
    };

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

    uint16_t CalcCrc(const uint8_t* data, size_t size)
    {
        auto crc = CRC16::CalculateCRC16(data, size);
        return ((crc >> 8) & 0xFF) | ((crc << 8) & 0xFF00);
    }

    void Check(uint16_t address, size_t size, uint8_t fn, uint8_t dataAddress, const std::vector<uint8_t>& bytes)
    {
        if (bytes.size() != size) {
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
    config->FrameTimeout = std::max(config->FrameTimeout, port->GetSendTime(3.5));
}

std::vector<uint8_t> Dooya::TDevice::ExecCommand(const TRequest& request)
{
    Port()->WriteBytes(request.Data);
    std::vector<uint8_t> respBytes(request.ResponseSize);
    auto bytesRead = Port()->ReadFrame(respBytes.data(),
                                       respBytes.size(),
                                       DeviceConfig()->ResponseTimeout,
                                       DeviceConfig()->FrameTimeout);
    respBytes.resize(bytesRead);
    return respBytes;
}

void Dooya::TDevice::WriteRegisterImpl(PRegister reg, const TRegisterValue& regValue)
{
    auto value = regValue.Get<uint64_t>();
    switch (reg->Type) {
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
            uint8_t dataAddress = GetUint32RegisterAddress(reg->GetAddress());
            TRequest req;
            req.Data = MakeRequest(SlaveId, {WRITE, dataAddress, 1, static_cast<uint8_t>(value)});
            req.ResponseSize = RESPONSE_SIZE;
            if (ParseReadResponse(SlaveId, WRITE, dataAddress, ExecCommand(req)) != 1) {
                throw TSerialDeviceTransientErrorException("Bad response");
            }
            return;
        }
        case COMMAND: {
            uint8_t dataAddress = GetUint32RegisterAddress(reg->GetAddress());
            TRequest req;
            req.Data = MakeRequest(SlaveId, {CONTROL, dataAddress});
            req.ResponseSize = CONTROL_RESPONSE_SIZE;
            if (req.Data != ExecCommand(req)) {
                throw TSerialDeviceTransientErrorException("Bad response");
            }
            return;
        }
    }
}

TRegisterValue Dooya::TDevice::ReadRegisterImpl(PRegister reg)
{
    switch (reg->Type) {
        case POSITION: {
            return TRegisterValue{
                ParsePositionResponse(SlaveId, READ, GET_POSITION_DATA_LENGTH, ExecCommand(GetPositionCommand))};
        }
        case PARAM: {
            auto addr = GetUint32RegisterAddress(reg->GetAddress());
            TRequest req;
            req.Data = MakeRequest(SlaveId, {READ, static_cast<uint8_t>(addr & 0xFF), 1});
            req.ResponseSize = RESPONSE_SIZE;
            return TRegisterValue{ParseReadResponse(SlaveId, READ, 1, ExecCommand(req))};
        }
        case COMMAND: {
            return TRegisterValue{1};
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
