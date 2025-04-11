#include "rpc_port_load_request.h"
#include "rpc_serial_port_settings.h"

namespace
{
    std::string ByteVectorToHexString(const std::vector<uint8_t>& byteVector)
    {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');

        for (const auto& byte: byteVector) {
            ss << std::hex << std::setw(2) << static_cast<int>(byte);
        }

        return ss.str();
    }
}

std::vector<uint8_t> HexStringToByteVector(const std::string& hexString)
{
    std::vector<uint8_t> byteVector;
    if (hexString.size() % 2 != 0) {
        throw std::runtime_error("Hex message has odd char count");
    }

    for (size_t i = 0; i < hexString.size(); i += 2) {
        auto byte = strtol(hexString.substr(i, 2).c_str(), NULL, 16);
        byteVector.push_back(byte);
    }

    return byteVector;
}

void ParseRPCPortLoadRequest(const Json::Value& data, TRPCPortLoadRequest& request)
{

    std::string messageStr, formatStr;
    WBMQTT::JSON::Get(data, "format", formatStr);
    WBMQTT::JSON::Get(data, "msg", messageStr);

    if (formatStr == "HEX") {
        request.Format = TRPCMessageFormat::RPC_MESSAGE_FORMAT_HEX;
    } else {
        request.Format = TRPCMessageFormat::RPC_MESSAGE_FORMAT_STR;
    }

    if (request.Format == TRPCMessageFormat::RPC_MESSAGE_FORMAT_HEX) {
        request.Message = HexStringToByteVector(messageStr);
    } else {
        request.Message.assign(messageStr.begin(), messageStr.end());
    }

    WBMQTT::JSON::Get(data, "response_timeout", request.ResponseTimeout);
    WBMQTT::JSON::Get(data, "frame_timeout", request.FrameTimeout);
    WBMQTT::JSON::Get(data, "total_timeout", request.TotalTimeout);

    request.SerialPortSettings = ParseRPCSerialPortSettings(data);
}

std::string FormatResponse(const std::vector<uint8_t>& response, TRPCMessageFormat format)
{
    std::string responseStr;
    if (format == TRPCMessageFormat::RPC_MESSAGE_FORMAT_HEX) {
        responseStr = ByteVectorToHexString(response);
    } else {
        responseStr.assign(response.begin(), response.end());
    }

    return responseStr;
}