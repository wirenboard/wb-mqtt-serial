
#pragma once
#include <serial_driver.h>
#include <wblib/rpc.h>

enum class RPCPortConfigSet
{
    RPC_TCP_SET,
    RPC_SERIAL_SET
};

enum class RPCPortHandlerResult
{
    RPC_OK = 0,
    RPC_WRONG_PARAM_SET = -1,
    RPC_WRONG_PARAM_VALUE = -2,
    RPC_WRONG_PORT = -3,
    RPC_WRONG_IO = -4,
    RPC_WRONG_RESP_LNGTH = -5
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

class TRPCHandler
{
public:
    TRPCHandler(PMQTTSerialDriver serialDriver, WBMQTT::PMqttRpcServer rpcServer);

private:
    PMQTTSerialDriver SerialDriver;
    WBMQTT::PMqttRpcServer RpcServer;
    TRPCPortConfig Config;

    Json::Value PortLoad(const Json::Value& request);
    Json::Value LoadMetrics(const Json::Value& request);
};

class TRPCException: public std::runtime_error
{
public:
    TRPCException(const std::string& message, RPCPortHandlerResult resultCode)
        : std::runtime_error(message),
          Message(message),
          ResultCode(resultCode)
    {}

    RPCPortHandlerResult GetResultCode()
    {
        return ResultCode;
    }

    std::string GetResultMessage()
    {
        return Message;
    }

private:
    std::string Message;
    RPCPortHandlerResult ResultCode;
};