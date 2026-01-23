#pragma once

#include <wblib/json_utils.h>
#include <wblib/rpc.h>

#include "port/serial_port_settings.h"
#include "rpc_port_handler.h"
#include "serial_device.h"

enum class TRPCMessageFormat
{
    RPC_MESSAGE_FORMAT_HEX,
    RPC_MESSAGE_FORMAT_STR
};

class TRPCPortLoadRequestBase
{
public:
    std::vector<uint8_t> Message;
    std::chrono::milliseconds ResponseTimeout = DEFAULT_RESPONSE_TIMEOUT;
    std::chrono::milliseconds FrameTimeout = DefaultFrameTimeout;
    std::chrono::milliseconds TotalTimeout = DefaultRPCTotalTimeout;
    TRPCMessageFormat Format;

    WBMQTT::TMqttRpcServer::TResultCallback OnResult = nullptr;
    WBMQTT::TMqttRpcServer::TErrorCallback OnError = nullptr;
};

class TRPCPortLoadRequest: public TRPCPortLoadRequestBase
{
public:
    TSerialPortConnectionSettings SerialPortSettings;
};

typedef std::shared_ptr<TRPCPortLoadRequest> PRPCPortLoadRequest;

void ParseRPCPortLoadRequestBase(const Json::Value& data, TRPCPortLoadRequestBase& request);
void ParseRPCPortLoadRequest(const Json::Value& data, TRPCPortLoadRequest& request);
std::string FormatResponse(const std::vector<uint8_t>& response, TRPCMessageFormat format);
std::vector<uint8_t> HexStringToByteVector(const std::string& hexString);
