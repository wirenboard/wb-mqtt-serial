#pragma once
#include "rpc_port.h"
#include <wblib/json_utils.h>

class TRPCRequest
{
public:
    std::vector<uint8_t> Message;
    std::chrono::milliseconds ResponseTimeout;
    std::chrono::milliseconds FrameTimeout;
    std::chrono::milliseconds TotalTimeout;
    std::string Format;
    size_t ResponseSize;
};

typedef std::shared_ptr<TRPCRequest> PRPCRequest;
