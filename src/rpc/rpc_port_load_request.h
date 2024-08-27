#pragma once

#include <wblib/json_utils.h>
#include <wblib/rpc.h>

#include "serial_device.h"
#include "serial_port_settings.h"

const std::chrono::seconds DefaultRPCTotalTimeout(10);

enum class TRPCMessageFormat
{
    RPC_MESSAGE_FORMAT_HEX,
    RPC_MESSAGE_FORMAT_STR
};

class TRPCPortLoadRequest
{
public:
    std::vector<uint8_t> Message;
    std::chrono::milliseconds ResponseTimeout = DefaultResponseTimeout;
    std::chrono::milliseconds FrameTimeout = DefaultFrameTimeout;
    std::chrono::milliseconds TotalTimeout = DefaultRPCTotalTimeout;
    TRPCMessageFormat Format;

    TSerialPortConnectionSettings SerialPortSettings;

    WBMQTT::TMqttRpcServer::TResultCallback OnResult = nullptr;
    WBMQTT::TMqttRpcServer::TErrorCallback OnError = nullptr;
};

typedef std::shared_ptr<TRPCPortLoadRequest> PRPCPortLoadRequest;

TSerialPortConnectionSettings ParseRPCSerialPortSettings(const Json::Value& request);
void ParseRPCPortLoadRequest(const Json::Value& data, TRPCPortLoadRequest& request);
std::string FormatResponse(const std::vector<uint8_t>& response, TRPCMessageFormat format);
std::vector<uint8_t> HexStringToByteVector(const std::string& hexString);
