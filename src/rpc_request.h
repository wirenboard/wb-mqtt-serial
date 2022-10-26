#pragma once
#include "rpc_port.h"
#include <wblib/json_utils.h>

enum class TRPCMessageFormat
{
    RPC_MESSAGE_FORMAT_HEX,
    RPC_MESSAGE_FORMAT_STR
};

class TRPCRequest
{
public:
    std::vector<uint8_t> Message;
    std::chrono::milliseconds ResponseTimeout;
    std::chrono::milliseconds FrameTimeout;
    std::chrono::milliseconds TotalTimeout;
    TRPCMessageFormat Format;
    size_t ResponseSize;

    int BaudRate;
    char Parity;
    int DataBits;
    int StopBits;
};

typedef std::shared_ptr<TRPCRequest> PRPCRequest;
