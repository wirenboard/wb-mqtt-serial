#include "energomera_ce_device.h"
#include "bin_utils.h"

using namespace BinUtils;

namespace
{
    const uint8_t END = 0xC0;
    const uint8_t OPT = 0x48;

    // Taken from example in the documentation
    const uint16_t ADDREIA = 253;

    const uint32_t USR_PASSWORD = 0x00000000;

    const uint8_t CLASS_ACCESS_COMMAND = 0x05;
    const uint8_t CLASS_ACCESS_ERROR = 0x07;

    const uint8_t DIRECT_REQUEST = 0x01;
    // const uint8_t DIRECT_RESPONSE = 0x00;

    const size_t PACKET_ENVELOPE_SIZE = 8;
    const size_t RESPONSE_HEADER_SIZE = 3;
    const size_t MAX_PAYLOAD_SIZE = 15;
    const size_t MAX_RESPONSE_SIZE = (PACKET_ENVELOPE_SIZE + RESPONSE_HEADER_SIZE + MAX_PAYLOAD_SIZE) * 2;
    const size_t MIN_RESPONSE_SIZE = PACKET_ENVELOPE_SIZE + RESPONSE_HEADER_SIZE + 1;

    const size_t RESPONSE_SERV_POS = 6;
    const size_t RESPONSE_DATA_POS = 9;

    const size_t ENERGY_RESPONSE_DATA_SIZE = 7;
    const size_t DATE_TIME_RESPONSE_DATA_SIZE = 7;

    const std::unordered_map<uint8_t, std::vector<uint8_t>> BYTE_STUFFING_RULES = {{0xC0, {0xDB, 0xDC}},
                                                                                   {0xDB, {0xDB, 0xDD}}};

    enum TEnergomeraCeRegisterType
    {
        DEFAULT,
        DATE,
        ENERGY
    };

    const TRegisterTypes RegisterTypes{{TEnergomeraCeRegisterType::DEFAULT, "default", "value", U16, true},
                                       {TEnergomeraCeRegisterType::DATE, "date", "text", String, true},
                                       {TEnergomeraCeRegisterType::ENERGY, "energy", "value", U32, true}};

    // Calc crc8 polynomial 0xB5
    uint8_t CalcCrc(std::vector<uint8_t>::const_iterator begin, std::vector<uint8_t>::const_iterator end)
    {
        uint8_t crc = 0x00;
        for (auto it = begin; it != end; ++it) {
            crc ^= *it;
            for (size_t i = 0; i < 8; ++i) {
                crc = crc & 0x80 ? (crc << 1) ^ 0xB5 : crc << 1;
            }
        }
        return crc;
    }

    uint8_t MakeServField(size_t messageLength)
    {
        return (DIRECT_REQUEST << 7) | (CLASS_ACCESS_COMMAND << 4) | (messageLength & 0x0F);
    }

    std::vector<uint8_t> MakePacket(uint16_t addr,
                                    const std::vector<uint8_t>& password,
                                    const std::vector<uint8_t>& data)
    {
        std::vector<uint8_t> content;
        auto contentBack = std::back_inserter(content);
        Append(contentBack, OPT);
        Append(contentBack, addr);
        Append(contentBack, ADDREIA);
        if (password.empty()) {
            Append(contentBack, USR_PASSWORD);
        } else {
            std::copy(password.begin(), password.end(), contentBack);
        }
        size_t dataSize = data.size() > 2 ? data.size() - 2 : 0;
        Append(contentBack, MakeServField(dataSize));
        std::copy(data.begin(), data.end(), contentBack);
        Append(contentBack, CalcCrc(content.begin(), content.end()));

        std::vector<uint8_t> packet;
        auto packetBack = std::back_inserter(packet);
        Append(packetBack, END);
        ApplyByteStuffing(content, BYTE_STUFFING_RULES, packetBack);
        Append(packetBack, END);
        return packet;
    }

    std::vector<uint8_t> MakeCommandFromRegisterAddress(uint32_t addr)
    {
        std::vector<uint8_t> data;
        uint8_t byte = (addr >> 24) & 0xFF;
        if (!data.empty() || byte != 0) {
            data.push_back(byte);
        }
        byte = (addr >> 16) & 0xFF;
        if (!data.empty() || byte != 0) {
            data.push_back(byte);
        }
        byte = (addr >> 8) & 0xFF;
        data.push_back(byte);
        byte = addr & 0xFF;
        data.push_back(byte);
        return data;
    }

    void CheckResponse(std::vector<uint8_t>& data)
    {
        if (data.size() < MIN_RESPONSE_SIZE) {
            throw TSerialDeviceTransientErrorException("frame too short");
        }
        if (data.front() != END || data.back() != END || data[1] != OPT) {
            throw TSerialDeviceTransientErrorException("invalid packet");
        }
        if (CalcCrc(data.begin() + 1, data.end() - 2) != *(data.end() - 2)) {
            throw TSerialDeviceTransientErrorException("invalid CRC");
        }
        if (((data[RESPONSE_SERV_POS] >> 4) & 0x7) == CLASS_ACCESS_ERROR) {
            throw TSerialDeviceTransientErrorException("error in response, code: " +
                                                       WBMQTT::HexDump(&data[RESPONSE_DATA_POS], 1));
        }
    }

    size_t GetResponseDataSize(const std::vector<uint8_t>& data)
    {
        return data.size() - PACKET_ENVELOPE_SIZE - RESPONSE_HEADER_SIZE;
    }

    TRegisterValue GetValue(const std::vector<uint8_t>& data)
    {
        const auto dataSize = GetResponseDataSize(data);
        switch (dataSize) {
            case 1:
                return TRegisterValue(data[RESPONSE_DATA_POS]);
            case 2:
                return TRegisterValue(GetFromBigEndian<uint16_t>(data.begin() + RESPONSE_DATA_POS));
            case 3:
                return TRegisterValue(
                    GetBigEndian<uint32_t>(data.begin() + RESPONSE_DATA_POS, data.begin() + RESPONSE_DATA_POS + 3));
            case 4:
                return TRegisterValue(GetFromBigEndian<uint32_t>(data.begin() + RESPONSE_DATA_POS));
            default:
                throw TSerialDeviceTransientErrorException("Data size is too big");
        }
    }

    TRegisterValue GetDate(const std::vector<uint8_t>& data)
    {
        if (GetResponseDataSize(data) != DATE_TIME_RESPONSE_DATA_SIZE) {
            throw TSerialDeviceTransientErrorException("Data size mismatch");
        }
        std::stringstream ss;
        ss << std::hex << std::setfill('0') << std::setw(2) << int(data[RESPONSE_DATA_POS + 4]) << "." << std::setw(2)
           << int(data[RESPONSE_DATA_POS + 5]) << "." << std::setw(2) << int(data[RESPONSE_DATA_POS + 6]) << " "
           << std::setw(2) << int(data[RESPONSE_DATA_POS + 2]) << ":" << std::setw(2)
           << int(data[RESPONSE_DATA_POS + 1]) << ":" << std::setw(2) << int(data[RESPONSE_DATA_POS]);
        return TRegisterValue(ss.str());
    }

    TRegisterValue GetEnergy(const std::vector<uint8_t>& data)
    {
        if (GetResponseDataSize(data) != ENERGY_RESPONSE_DATA_SIZE) {
            throw TSerialDeviceTransientErrorException("Data size mismatch");
        }
        return TRegisterValue(GetFrom<uint32_t>(data.begin() + RESPONSE_DATA_POS + 3));
    }

    bool FrameComplete(uint8_t* buf, int size)
    {
        return (static_cast<size_t>(size) >= MIN_RESPONSE_SIZE && buf[0] == END && buf[size - 1] == END);
    }
}

void TEnergomeraCeDevice::Register(TSerialDeviceFactory& factory)
{
    factory.RegisterProtocol(
        new TUint32SlaveIdProtocol("energomera_ce", RegisterTypes),
        new TBasicDeviceFactory<TEnergomeraCeDevice>("#/definitions/simple_device", "#/definitions/common_channel"));
}

TEnergomeraCeDevice::TEnergomeraCeDevice(PDeviceConfig config, PProtocol protocol)
    : TSerialDevice(config, protocol),
      TUInt32SlaveId(config->SlaveId)
{}

TRegisterValue TEnergomeraCeDevice::ReadRegisterImpl(TPort& port, const TRegisterConfig& reg)
{
    auto request = MakePacket(SlaveId,
                              DeviceConfig()->Password,
                              MakeCommandFromRegisterAddress(GetUint32RegisterAddress(reg.GetAddress())));
    port.WriteBytes(request.data(), request.size());
    std::vector<uint8_t> response(MAX_RESPONSE_SIZE);
    auto res = port.ReadFrame(response.data(),
                              response.size(),
                              GetResponseTimeout(port),
                              GetFrameTimeout(port),
                              FrameComplete);
    response.resize(res.Count);
    DecodeByteStuffing(response, BYTE_STUFFING_RULES);
    CheckResponse(response);
    switch (reg.Type) {
        case TEnergomeraCeRegisterType::DEFAULT:
            return GetValue(response);
        case TEnergomeraCeRegisterType::DATE:
            return GetDate(response);
        case TEnergomeraCeRegisterType::ENERGY:
            return GetEnergy(response);
        default: {
            throw TSerialDevicePermanentRegisterException("Unknown register type " + std::to_string(reg.Type));
        }
    }
}
