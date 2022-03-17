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
            auto addr = LoadRegisterBitsAddress(regCfg);
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

TLLSDevice::TLLSDevice(PDeviceConfig config, PPort port, PProtocol protocol)
    : TSerialDevice(config, port, protocol),
      TUInt32SlaveId(config->SlaveId)
{
    auto timeout = port->GetSendTime(3.5) + std::chrono::milliseconds(1);
    config->FrameTimeout = std::max(config->FrameTimeout, timeout);
}

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

std::vector<uint8_t> TLLSDevice::ExecCommand(uint8_t cmd)
{
    auto it = CmdResultCache.find(cmd);
    if (it != CmdResultCache.end()) {
        return it->second;
    }

    Port()->CheckPortOpen();
    Port()->SkipNoise();

    uint8_t buf[RESPONSE_BUF_LEN] = {};
    buf[0] = REQUEST_PREFIX;
    buf[1] = SlaveId;
    buf[2] = cmd;
    buf[3] = dallas_crc8(buf, REQUEST_LEN - 1);
    Port()->WriteBytes(buf, REQUEST_LEN);
    Port()->SleepSinceLastInteraction(DeviceConfig()->FrameTimeout);

    int len = Port()->ReadFrame(buf, RESPONSE_BUF_LEN, DeviceConfig()->ResponseTimeout, DeviceConfig()->FrameTimeout);
    if (buf[0] != 0x3e) {
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

uint64_t TLLSDevice::ReadRegisterImpl(PRegister reg)
{
    uint8_t cmd = GetUint32RegisterAddress(reg->GetAddress());
    auto result = ExecCommand(cmd);

    int result_buf[8] = {};

    for (int i = 0; i < reg->GetByteWidth(); ++i) {
        result_buf[i] = result[reg->DataOffset + i];
    }

    return (result_buf[3] << 24) | (result_buf[2] << 16) | (result_buf[1] << 8) | result_buf[0];
}
