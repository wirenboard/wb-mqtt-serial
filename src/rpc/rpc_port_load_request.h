#pragma once

#include <wblib/json_utils.h>
#include <wblib/rpc.h>

#include "serial_port_settings.h"

enum class TRPCMessageFormat
{
    RPC_MESSAGE_FORMAT_HEX,
    RPC_MESSAGE_FORMAT_STR
};

class TRPCPortLoadRequest
{
public:
    std::vector<uint8_t> Message;
    std::chrono::milliseconds ResponseTimeout;
    std::chrono::milliseconds FrameTimeout;
    std::chrono::milliseconds TotalTimeout;
    TRPCMessageFormat Format;
    size_t ResponseSize;

    TSerialPortConnectionSettings SerialPortSettings;

    std::function<void(const std::vector<uint8_t>&)> OnResult = nullptr;
    WBMQTT::TMqttRpcServer::TErrorCallback OnError = nullptr;
};

typedef std::shared_ptr<TRPCPortLoadRequest> PRPCPortLoadRequest;
