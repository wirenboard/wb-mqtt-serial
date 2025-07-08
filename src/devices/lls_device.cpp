#include "lls_device.h"

#include <stddef.h>

namespace
{
    class TLLSDeviceRegisterAddressFactory: public TStringRegisterAddressFactory
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

void TLLSDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(
        new TUint32SlaveIdProtocol("lls", TRegisterTypes({{0, "default", "value", Float, true}})),
        new TBasicDeviceFactory<TLLSDevice, TLLSDeviceRegisterAddressFactory>("#/definitions/simple_device",
                                                                              "#/definitions/common_channel"));
}

TLLSDevice::TLLSDevice(PDeviceConfig config, PProtocol protocol)
    : TSerialDevice(config, protocol),
      TUInt32SlaveId(config->SlaveId)
{}

static unsigned char dallas_crc8(const unsigned char* data, const unsigned int size)
{
    unsigned char crc = 0;
    for (unsigned int i = 0; i < size; ++i) {
        unsigned char inbyte = data[i];
        for (unsigned char j = 0; j < 8; ++j) {
            unsigned char mix = (crc ^ inbyte) & 0x01;
            crc >>= 1;
            if (mix)
                crc ^= 0x8C;
            inbyte >>= 1;
        }
    }
    return crc;
}

void TLLSDevice::InvalidateReadCache()
{
    CmdResultCache.clear();

    TSerialDevice::InvalidateReadCache();
}

namespace
{
    const size_t RESPONSE_BUF_LEN = 100;
    const size_t REQUEST_LEN = 4;
    const ptrdiff_t HEADER_SZ = 3;
    const uint8_t REQUEST_PREFIX = 0x31;
    const uint8_t RESPONSE_PREFIX = 0x3E;
}

std::vector<uint8_t> TLLSDevice::ExecCommand(TPort& port, uint8_t cmd)
{
    auto it = CmdResultCache.find(cmd);
    if (it != CmdResultCache.end()) {
        return it->second;
    }

    port.CheckPortOpen();
    port.SkipNoise();

    uint8_t buf[RESPONSE_BUF_LEN] = {};
    buf[0] = REQUEST_PREFIX;
    buf[1] = SlaveId;
    buf[2] = cmd;
    buf[3] = dallas_crc8(buf, REQUEST_LEN - 1);
    port.WriteBytes(buf, REQUEST_LEN);
    port.SleepSinceLastInteraction(GetFrameTimeout(port));

    int len = port.ReadFrame(buf, RESPONSE_BUF_LEN, GetResponseTimeout(port), GetFrameTimeout(port)).Count;
    if (buf[0] != RESPONSE_PREFIX) {
        throw TSerialDeviceTransientErrorException("invalid response prefix");
    }
    if (buf[1] != SlaveId) {
        throw TSerialDeviceTransientErrorException("invalid response network address");
    }
    if (buf[2] != cmd) {
        throw TSerialDeviceTransientErrorException("invalid response cmd");
    }
    if (buf[len - 1] != dallas_crc8(buf, len - 1)) {
        throw TSerialDeviceTransientErrorException("invalid response crc");
    }

    uint8_t* payload = buf + HEADER_SZ;
    std::vector<uint8_t> result = {0};
    result.assign(payload, payload + len - HEADER_SZ);
    return CmdResultCache.insert({cmd, result}).first->second;
}

TRegisterValue TLLSDevice::ReadRegisterImpl(TPort& port, const TRegisterConfig& reg)
{
    uint8_t cmd = GetUint32RegisterAddress(reg.GetAddress());
    auto result = ExecCommand(port, cmd);

    int result_buf[8] = {};

    for (uint32_t i = 0; i < reg.GetByteWidth(); ++i) {
        result_buf[i] = result[reg.GetDataOffset() + i];
    }

    return TRegisterValue{
        static_cast<uint64_t>((result_buf[3] << 24) | (result_buf[2] << 16) | (result_buf[1] << 8) | result_buf[0])};
}

std::chrono::milliseconds TLLSDevice::GetFrameTimeout(TPort& port) const
{
    auto timeout =
        std::chrono::ceil<std::chrono::milliseconds>(port.GetSendTimeBytes(3.5)) + std::chrono::milliseconds(1);
    return std::max(DeviceConfig()->FrameTimeout, timeout);
}
