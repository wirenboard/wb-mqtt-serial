#include "somfy_sdn_device.h"
#include "bin_utils.h"
#include "log.h"

using namespace BinUtils;

namespace
{
    enum TRegTypes
    {
        POSITION,
        PARAM,
        COMMAND
    };

    const TRegisterTypes RegTypes{{POSITION, "position", "value", U8},
                                  {PARAM, "param", "value", U64, true},
                                  {COMMAND, "command", "value", U64}};

    const size_t CRC_SIZE = 2;
    const uint32_t HOST_ADDRESS = 0x0000FFFF;
    const size_t ADDRESS_SIZE = 3;
    const size_t SOURCE_START = 3;
    const size_t SOURCE_END = SOURCE_START + ADDRESS_SIZE;
    const size_t DESTINATION_START = SOURCE_END;
    const size_t DESTINATION_END = DESTINATION_START + ADDRESS_SIZE;
    const size_t DATA_POS = DESTINATION_END;

    const size_t MAX_RESPONSE_SIZE = 32;
    const size_t LEN_MASK = 0x1F; // 5 bits are reserved for packet length

    size_t GetLen(uint8_t data)
    {
        return (data & LEN_MASK);
    }

    uint16_t CalcCrc(std::vector<uint8_t>::const_iterator begin, std::vector<uint8_t>::const_iterator end)
    {
        uint16_t sum = 0;
        for (; begin != end; ++begin) {
            sum += ((~(*begin)) & 0xFF);
        }
        return sum;
    }

    void Check(uint32_t address, uint8_t header, const std::vector<uint8_t>& bytes)
    {
        size_t len = GetLen(bytes[1]);
        if (bytes.size() < 2 || bytes.size() != len) {
            throw TSerialDeviceTransientErrorException("Bad response size");
        }

        auto calculatedCrc = CalcCrc(bytes.begin(), bytes.end() - CRC_SIZE);
        auto crc = GetBigEndian<uint16_t>(bytes.end() - CRC_SIZE, bytes.end());

        if (crc != calculatedCrc) {
            throw TSerialDeviceTransientErrorException("Bad CRC");
        }
        if (bytes[0] != header) {
            if (bytes[0] == Somfy::NACK) {
                throw TSerialDeviceInternalErrorException("NACK: " + std::to_string(static_cast<int>(bytes[DATA_POS])));
            }
            throw TSerialDeviceTransientErrorException("Bad header");
        }
        if (Get<uint32_t>(bytes.begin() + DESTINATION_START, bytes.begin() + DESTINATION_END) != HOST_ADDRESS) {
            TSerialDeviceTransientErrorException("Bad destination address");
        }
        if (Get<uint32_t>(bytes.begin() + SOURCE_START, bytes.begin() + SOURCE_END) != address) {
            throw TSerialDeviceTransientErrorException("Bad source address");
        }
    }

    bool FrameComplete(uint8_t* buf, int size)
    {
        if (static_cast<size_t>(size) < 2 + CRC_SIZE + 2 * ADDRESS_SIZE) {
            return false;
        }
        return static_cast<size_t>(size) == ~GetLen(buf[1]);
    }

    class TSomfyAddress: public TUint32RegisterAddress
    {
        uint8_t ResponseHeader;

    public:
        TSomfyAddress(uint8_t requestHeader, uint8_t responseHeader)
            : TUint32RegisterAddress(requestHeader),
              ResponseHeader(responseHeader)
        {}

        uint8_t GetResponseHeader() const
        {
            return ResponseHeader;
        }
    };

    uint8_t GetResponseAddress(const Json::Value& regCfg)
    {
        const auto& rh = regCfg.get("response_header", Somfy::ACK);
        if (rh.isUInt()) {
            return rh.asUInt();
        }
        if (!rh.isString()) {
            throw TConfigParserException("response_header: unsigned integer or '0x..' hex string expected");
        }
        auto str = rh.asString();
        char* end;
        auto res = strtoul(str.c_str(), &end, 16);
        if (*end == 0 && end != str.c_str()) {
            return res;
        }
        res = strtoul(str.c_str(), &end, 10);
        if (*end == 0 && end != str.c_str()) {
            return res;
        }
        throw TConfigParserException("response_header: unsigned integer or '0x..' hex string expected instead of '" +
                                     str + "'");
    }

    class TSomfyAddressFactory: public TUint32RegisterAddressFactory
    {
    public:
        TRegisterDesc LoadRegisterAddress(const Json::Value& regCfg,
                                          const IRegisterAddress& deviceBaseAddress,
                                          uint32_t stride,
                                          uint32_t registerByteWidth) const override
        {
            auto addr = LoadRegisterBitsAddress(regCfg, SerialConfig::ADDRESS_PROPERTY_NAME);
            TRegisterDesc res;
            res.BitOffset = addr.BitOffset;
            res.BitWidth = addr.BitWidth;
            res.Address = std::make_shared<TSomfyAddress>(addr.Address, GetResponseAddress(regCfg));
            return res;
        }
    };

    class TSomfyDeviceFactory: public IDeviceFactory
    {
    public:
        TSomfyDeviceFactory()
            : IDeviceFactory(std::make_unique<TSomfyAddressFactory>(), "#/definitions/somfy_device_no_channels")
        {}

        PSerialDevice CreateDevice(const Json::Value& data,
                                   PDeviceConfig deviceConfig,
                                   PPort port,
                                   PProtocol protocol) const override
        {
            uint8_t nodeType = data.get("node_type", Somfy::SONESSE_30).asUInt();
            return std::make_shared<Somfy::TDevice>(deviceConfig, nodeType, port, protocol);
        }
    };

    void PrintDebugDump(const std::vector<uint8_t>& data, const char* msg)
    {
        if (Debug.IsEnabled()) {
            std::stringstream ss;
            for (auto b: data) {
                ss << std::hex << std::setw(2) << std::setfill('0') << int(b) << " ";
            }
            Debug.Log() << "[Somfy] " << msg << ss.str();
        }
    }
}

void Somfy::TDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(new TUint32SlaveIdProtocol("somfy", RegTypes), new TSomfyDeviceFactory());
}

Somfy::TDevice::TDevice(PDeviceConfig config, uint8_t nodeType, PPort port, PProtocol protocol)
    : TSerialDevice(config, port, protocol),
      TUInt32SlaveId(config->SlaveId),
      OpenCommand{MakeRequest(Somfy::CTRL_MOVETO, SlaveId, nodeType, {0, 0, 0, 0})},
      CloseCommand{MakeRequest(Somfy::CTRL_MOVETO, SlaveId, nodeType, {1, 0, 0, 0})},
      NodeType(nodeType)
{}

std::vector<uint8_t> Somfy::TDevice::ExecCommand(const std::vector<uint8_t>& request)
{
    Port()->SleepSinceLastInteraction(DeviceConfig()->FrameTimeout);
    Port()->WriteBytes(request);
    std::vector<uint8_t> respBytes(MAX_RESPONSE_SIZE);
    auto bytesRead = Port()->ReadFrame(respBytes.data(),
                                       respBytes.size(),
                                       DeviceConfig()->ResponseTimeout,
                                       DeviceConfig()->FrameTimeout,
                                       FrameComplete);
    respBytes.resize(bytesRead);
    FixReceivedFrame(respBytes);
    PrintDebugDump(respBytes, "Frame read: ");
    return respBytes;
}

void Somfy::TDevice::WriteRegisterImpl(PRegister reg, uint64_t value)
{
    if (reg->Type == POSITION) {
        if (value == 0) {
            Check(SlaveId, ACK, ExecCommand(CloseCommand));
            return;
        }
        if (value == 100) {
            Check(SlaveId, ACK, ExecCommand(OpenCommand));
            return;
        }
        Check(SlaveId, ACK, ExecCommand(MakeSetPositionRequest(SlaveId, NodeType, value)));
        return;
    }
    if (reg->Type == COMMAND) {
        std::vector<uint8_t> data;
        size_t l = (value & 0xFF);
        for (; l != 0; --l) {
            value >>= 8;
            data.push_back(value & 0xFF);
        }
        auto addr = GetUint32RegisterAddress(reg->GetAddress());
        Check(SlaveId, ACK, ExecCommand(MakeRequest(addr, SlaveId, NodeType, data)));
        return;
    }
    throw TSerialDevicePermanentRegisterException("Unsupported register type");
}

uint64_t Somfy::TDevice::GetCachedResponse(uint8_t requestHeader,
                                           uint8_t responseHeader,
                                           size_t bitOffset,
                                           size_t bitWidth)
{
    uint64_t val;
    auto it = DataCache.find(requestHeader);
    if (it != DataCache.end()) {
        val = it->second;
    } else {
        auto req = MakeRequest(requestHeader, SlaveId, NodeType);
        val = ParseStatusReport(SlaveId, responseHeader, ExecCommand(req));
        DataCache[requestHeader] = val;
    }
    if (bitOffset || bitWidth) {
        return (val >> bitOffset) & GetLSBMask(bitWidth);
    }
    return val;
}

uint64_t Somfy::TDevice::ReadRegisterImpl(PRegister reg)
{
    switch (reg->Type) {
        case POSITION: {
            auto res = GetCachedResponse(GET_MOTOR_POSITION, POST_MOTOR_POSITION, 2 * 8, 8);
            if (res > 100) {
                throw TSerialDeviceInternalErrorException("Unknown position");
            }
            return res;
        }
        case PARAM: {
            const auto& addr = dynamic_cast<const TSomfyAddress&>(reg->GetAddress());
            return GetCachedResponse(addr.Get(),
                                     addr.GetResponseHeader(),
                                     reg->GetBitOffset(),
                                     reg->GetBitWidth(EDeviceType::SOMFY));
        }
        case COMMAND: {
            return 1;
        }
    }
    throw TSerialDevicePermanentRegisterException("Unsupported register type");
}

void Somfy::TDevice::EndPollCycle()
{
    DataCache.clear();
}

std::vector<uint8_t> Somfy::MakeRequest(uint8_t msg,
                                        uint32_t address,
                                        uint8_t nodeType,
                                        const std::vector<uint8_t>& data)
{
    std::vector<uint8_t> res{msg, 0x00, 0x00, 0xFF, 0xFF, 0x00};
    res[2] = (nodeType & 0x0F);
    auto it = std::back_inserter(res);
    Append(it, address, ADDRESS_SIZE);
    std::copy(data.begin(), data.end(), it);
    res[1] = (res.size() + CRC_SIZE) | 0x80; // MSB - ACK request
    auto crc = CalcCrc(res.begin(), res.end());
    res.push_back(crc >> 8);
    res.push_back(crc);
    PrintDebugDump(res, "Request: ");
    std::transform(res.begin(), res.end() - CRC_SIZE, res.begin(), std::bit_not<uint8_t>());
    return res;
}

std::vector<uint8_t> Somfy::MakeSetPositionRequest(uint32_t address, uint8_t nodeType, uint32_t position)
{
    return MakeRequest(Somfy::CTRL_MOVETO,
                       address,
                       nodeType,
                       {0x04, static_cast<uint8_t>(position & 0xFF), 0x00, 0x00});
}

uint64_t Somfy::ParseStatusReport(uint32_t address, uint8_t header, const std::vector<uint8_t>& bytes)
{
    Check(address, header, bytes);
    return Get<uint64_t>(bytes.begin() + DATA_POS, bytes.end() - CRC_SIZE);
}

void Somfy::FixReceivedFrame(std::vector<uint8_t>& bytes)
{
    if (bytes.size() > CRC_SIZE) {
        std::transform(bytes.begin(), bytes.end() - CRC_SIZE, bytes.begin(), std::bit_not<uint8_t>());
    }
}
