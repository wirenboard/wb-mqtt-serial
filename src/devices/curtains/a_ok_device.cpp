#include "a_ok_device.h"
#include "bin_utils.h"

/**
 * A-OK communication protocol
 *
 * WB to motor frame:
 * +---------+------+-------+------+-----------------------+
 * |Head code|ID    |Chanel |Data  |Verify                 |
 * +---------+------+-------+------+-----------------------+
 * |0x9a     |1 byte|2 bytes|2 byte|1 byte: ID^Channel^Data|
 * +---------+------+-------+------+-----------------------+
 *
 * Motor to WB frame (for status inquiry commands only):
 * +---------+------+-------+------+-----------------------+
 * |Head code|ID    |Chanel |Data  |Verify                 |
 * +---------+------+-------+------+-----------------------+
 * |0xd8     |1 byte|2 bytes|5 byte|1 byte: ID^Channel^Data|
 * +---------+------+-------+------+-----------------------+
 */

using namespace BinUtils;

namespace
{
    enum TRegTypes
    {
        POSITION,
        COMMAND,
        PARAM,
        STATUS,
        ZONEBIT
    };

    const TRegisterTypes RegTypes{{POSITION, "position", "value", U8},
                                  {COMMAND, "command", "value", U8},
                                  {PARAM, "param", "value", U8},
                                  {STATUS, "status", "value", U64},
                                  {ZONEBIT, "zonebit", "value", U8}};

    enum THeadCodes
    {
        REQUEST = 0x9a,
        RESPONSE = 0xd8
    };

    enum TCommandTypes
    {
        CONTROL = 0x0a,
        SET_POSITION = 0xdd,
        SET_CURTAIN_MOTOR_SETTING = 0xd5,
        MOTOR_STATUS = 0xcc,
        CURTAIN_MOTOR_STATUS = 0xca,
        TUBULAR_MOTOR_STATUS = 0xcb
    };

    const size_t CRC_SIZE = 1;
    const size_t MOTOR_STATUS_RESPONSE_SIZE = 10;
    const size_t MOTOR_STATUS_DATA_OFFSET = 4;
    // const size_t MOTOR_STATUS_DATA_SIZE = 5;
    const size_t MOTOR_STATUS_POSITION_OFFSET = 3;
    const size_t MOTOR_STATUS_ZONEBIT_OFFSET = 4;

    const size_t MOTOR_ID_POS = 1;
    const size_t LOW_CHANNEL_ID_POS = 2;
    const size_t HIGH_CHANNEL_ID_POS = 3;

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
    // Deprecated, use "a_ok" protocol instead, for backward compatibility only
    factory.RegisterProtocol(new TUint32SlaveIdProtocol("dauerhaft", RegTypes),
                             new TBasicDeviceFactory<Aok::TDevice>("#/definitions/simple_device_no_channels"));
}

Aok::TDevice::TDevice(PDeviceConfig config, PProtocol protocol)
    : TSerialDevice(config, protocol),
      TUInt32SlaveId(config->SlaveId),
      MotorId((SlaveId >> 16) & 0xFF),
      LowChannelId((SlaveId >> 8) & 0xFF),
      HighChannelId(SlaveId & 0xFF)
{}

TRegisterValue Aok::TDevice::GetCachedResponse(TPort& port,
                                               uint8_t command,
                                               uint8_t data,
                                               size_t bitOffset,
                                               size_t bitWidth)
{
    TRegisterValue val;
    uint16_t key = (command << 8) | data;
    auto it = DataCache.find(key);
    if (it != DataCache.end()) {
        val = it->second;
    } else {
        TRequest req;
        req.Data = MakeRequest(MotorId, LowChannelId, HighChannelId, command, data);
        req.ResponseSize = MOTOR_STATUS_RESPONSE_SIZE;
        auto resp = ExecCommand(port, req);
        // some tabular motors can answer any request,
        // so check that motor id in the response matches the requested one
        if (resp[MOTOR_ID_POS] != MotorId || resp[LOW_CHANNEL_ID_POS] != LowChannelId ||
            resp[HIGH_CHANNEL_ID_POS] != HighChannelId)
        {
            throw TSerialDeviceTransientErrorException("Invalid response");
        }
        val.Set(Get<uint64_t>(resp.begin() + MOTOR_STATUS_DATA_OFFSET, resp.end() - CRC_SIZE));
        DataCache[key] = val;
    }
    if (bitOffset || bitWidth) {
        return TRegisterValue{(val.Get<uint64_t>() >> bitOffset) & GetLSBMask(bitWidth)};
    }
    return val;
}

std::vector<uint8_t> Aok::TDevice::ExecCommand(TPort& port, const TRequest& request)
{
    port.SleepSinceLastInteraction(GetFrameTimeout(port));
    port.SkipNoise();
    port.WriteBytes(request.Data);
    std::vector<uint8_t> respBytes(request.ResponseSize);
    if (request.ResponseSize != 0) {
        auto bytesRead = port.ReadFrame(respBytes.data(),
                                        respBytes.size(),
                                        GetResponseTimeout(port),
                                        GetFrameTimeout(port),
                                        ExpectNBytes(request.ResponseSize))
                             .Count;
        respBytes.resize(bytesRead);
    }
    return respBytes;
}

TRegisterValue Aok::TDevice::ReadRegisterImpl(TPort& port, const TRegisterConfig& reg)
{
    switch (reg.Type) {
        case POSITION: {
            return GetCachedResponse(port, MOTOR_STATUS, 0, MOTOR_STATUS_POSITION_OFFSET * 8, 8);
        }
        case STATUS: {
            auto addr = GetUint32RegisterAddress(reg.GetAddress());
            return GetCachedResponse(port, (addr >> 8) & 0xFF, addr & 0xFF, reg.GetDataOffset(), reg.GetDataWidth());
        }
        case ZONEBIT: {
            auto addr = GetUint32RegisterAddress(reg.GetAddress());
            return GetCachedResponse(port,
                                     CURTAIN_MOTOR_STATUS,
                                     CURTAIN_MOTOR_STATUS,
                                     MOTOR_STATUS_ZONEBIT_OFFSET * 8 + addr,
                                     1);
        }
    }
    throw TSerialDevicePermanentRegisterException("Unsupported register type");
}

void Aok::TDevice::WriteRegisterImpl(TPort& port, const TRegisterConfig& reg, const TRegisterValue& regValue)
{
    auto value = regValue.Get<uint64_t>();
    switch (reg.Type) {
        case COMMAND: {
            auto addr = GetUint32RegisterAddress(reg.GetWriteAddress());
            TRequest req;
            req.Data = MakeRequest(MotorId, LowChannelId, HighChannelId, CONTROL, addr);
            ExecCommand(port, req);
            return;
        }
        case POSITION: {
            TRequest req;
            req.Data = MakeRequest(MotorId, LowChannelId, HighChannelId, SET_POSITION, value);
            ExecCommand(port, req);
            return;
        }
        case PARAM: {
            auto addr = GetUint32RegisterAddress(reg.GetWriteAddress());
            TRequest req;
            req.Data = MakeRequest(MotorId, LowChannelId, HighChannelId, addr, value);
            ExecCommand(port, req);
            return;
        }
        case ZONEBIT: {
            auto addr = GetUint32RegisterAddress(reg.GetWriteAddress());
            TRegisterValue val =
                GetCachedResponse(port, CURTAIN_MOTOR_STATUS, CURTAIN_MOTOR_STATUS, MOTOR_STATUS_ZONEBIT_OFFSET * 8, 8);
            TRequest req;
            req.Data = MakeRequest(MotorId,
                                   LowChannelId,
                                   HighChannelId,
                                   SET_CURTAIN_MOTOR_SETTING,
                                   (val.Get<uint8_t>() & ~(1 << addr)) | (value << addr));
            ExecCommand(port, req);
            return;
        }
    }
    throw TSerialDevicePermanentRegisterException("Unsupported register type");
}

void Aok::TDevice::InvalidateReadCache()
{
    DataCache.clear();
}

std::vector<uint8_t> Aok::MakeRequest(uint8_t id,
                                      uint8_t channelLow,
                                      uint8_t channelHigh,
                                      uint8_t command,
                                      uint8_t data)
{
    std::vector<uint8_t> res{REQUEST, id, channelLow, channelHigh, command, data, 0x00};
    res.back() = CalcCrc({res.begin() + 1, res.end() - CRC_SIZE});
    return res;
}
