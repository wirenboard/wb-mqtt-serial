#pragma once
#include "wblib/json_utils.h"

enum class RPCPortConfigSet
{
    RPC_TCP_SET,
    RPC_SERIAL_SET
};

class TRPCPortConfig
{
public:
    bool CheckParamSet(const Json::Value& request);
    bool LoadValues(const Json::Value& request);

    RPCPortConfigSet ParametersSet;
    std::string Path;
    std::string Ip;
    int Port;
    std::vector<uint8_t> Msg;
    std::chrono::microseconds ResponseTimeout;
    std::chrono::microseconds FrameTimeout;
    std::chrono::seconds TotalTimeout;
    std::string Format;
    size_t ResponseSize;
};