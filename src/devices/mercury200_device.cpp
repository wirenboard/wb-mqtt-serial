#include <chrono>
#include <cstdint>

#include "bcd_utils.h"
#include "crc16.h"
#include "mercury200_device.h"
#include "serial_device.h"

namespace
{
    const size_t RESPONSE_BUF_LEN = 100;
    const size_t REQUEST_LEN = 7;
    const ptrdiff_t HEADER_SZ = 5;

    const TRegisterTypes RegisterTypes{{0, "param8", "value", U8, true},
                                       {0, "param16", "value", BCD16, true},
                                       {0, "param24", "value", BCD24, true},
                                       {0, "param32", "value", BCD32, true}};

    class TMercury200DeviceRegisterAddressFactory: public TUint32RegisterAddressFactory
    {
    public:
        TRegisterDesc LoadRegisterAddress(const Json::Value& regCfg,
                                          const IRegisterAddress& deviceBaseAddress,
                                          uint32_t stride,
                                          uint32_t registerByteWidth) const override
        {
            auto addr = LoadRegisterBitsAddress(regCfg, SerialConfig::ADDRESS_PROPERTY_NAME);
            TRegisterDesc res;
            res.DataOffset = (addr.Address & 0xFF);
            res.Address = std::make_shared<TUint32RegisterAddress>(addr.Address >> 8);
            return res;
        }
    };
}

void TMercury200Device::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(new TUint32SlaveIdProtocol("mercury200", RegisterTypes),
                             new TBasicDeviceFactory<TMercury200Device, TMercury200DeviceRegisterAddressFactory>(
                                 "#/definitions/simple_device",
                                 "#/definitions/common_channel"));
}

TMercury200Device::TMercury200Device(PDeviceConfig config, PPort port, PProtocol protocol)
    : TSerialDevice(config, port, protocol),
      TUInt32SlaveId(config->SlaveId)
{
    config->FrameTimeout =
        std::max(config->FrameTimeout, std::chrono::ceil<std::chrono::milliseconds>(port->GetSendTimeBytes(6)));
}

std::vector<uint8_t> TMercury200Device::ExecCommand(uint8_t cmd)
{
    auto it = CmdResultCache.find(cmd);
    if (it != CmdResultCache.end()) {
        return it->second;
    }

    uint8_t buf[100] = {0x00};
    auto readn = RequestResponse(SlaveId, cmd, buf);
    if (readn < 4) { // fixme 4
        throw TSerialDeviceTransientErrorException("mercury200: read frame too short for command response");
    }
    if (IsBadHeader(SlaveId, cmd, buf)) {
        throw TSerialDeviceTransientErrorException("mercury200: bad response header for command");
    }
    if (IsCrcValid(buf, readn)) {
        throw TSerialDeviceTransientErrorException("mercury200: bad CRC for command");
    }
    uint8_t* payload = buf + HEADER_SZ;
    std::vector<uint8_t> result = {0};
    result.assign(payload, payload + readn - HEADER_SZ);
    return CmdResultCache.insert({cmd, result}).first->second;
}

TRegisterValue TMercury200Device::ReadRegisterImpl(const TRegisterConfig& reg)
{
    uint8_t cmd = (GetUint32RegisterAddress(reg.GetAddress()) & 0xFF);
    auto result = ExecCommand(cmd);
    auto size = RegisterFormatByteWidth(reg.Format);
    if (result.size() < reg.GetDataOffset() + size)
        throw TSerialDeviceException("mercury200: register address is out of range");

    return TRegisterValue{PackBytes(result.data() + reg.GetDataOffset(), static_cast<WordSizes>(size))};
}

void TMercury200Device::InvalidateReadCache()
{
    CmdResultCache.clear();

    TSerialDevice::InvalidateReadCache();
}

bool TMercury200Device::IsCrcValid(uint8_t* buf, int sz) const
{
    auto actual_crc = CRC16::CalculateCRC16(buf, static_cast<uint16_t>(sz));
    uint16_t sent_crc = ((uint16_t)buf[sz] << 8) | ((uint16_t)buf[sz + 1]);
    return actual_crc != sent_crc;
}

int TMercury200Device::RequestResponse(uint32_t slave, uint8_t cmd, uint8_t* response) const
{
    using namespace std::chrono;

    uint8_t request[REQUEST_LEN];
    FillCommand(request, slave, cmd);
    Port()->WriteBytes(request, REQUEST_LEN);
    return Port()
        ->ReadFrame(response, RESPONSE_BUF_LEN, DeviceConfig()->ResponseTimeout, DeviceConfig()->FrameTimeout)
        .Count;
}

void TMercury200Device::FillCommand(uint8_t* buf, uint32_t id, uint8_t cmd) const
{
    buf[0] = static_cast<uint8_t>(id >> 24);
    buf[1] = static_cast<uint8_t>(id >> 16);
    buf[2] = static_cast<uint8_t>(id >> 8);
    buf[3] = static_cast<uint8_t>(id);
    buf[4] = cmd;
    auto crc = CRC16::CalculateCRC16(buf, 5);
    buf[5] = static_cast<uint8_t>(crc >> 8);
    buf[6] = static_cast<uint8_t>(crc);
}

bool TMercury200Device::IsBadHeader(uint32_t slave_expected, uint8_t cmd_expected, uint8_t* response) const
{
    uint32_t actual_slave = ((uint32_t)response[0] << 24) | ((uint32_t)response[1] << 16) |
                            ((uint32_t)response[2] << 8) | (uint32_t)response[3];
    if (actual_slave != slave_expected) {
        return true;
    }
    return response[4] != cmd_expected;
}
