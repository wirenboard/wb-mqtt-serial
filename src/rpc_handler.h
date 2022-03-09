
#pragma once
#include "rpc_config.h"
#include <serial_driver.h>
#include <wblib/rpc.h>

enum class RPCPortHandlerResult
{
    RPC_OK = 0,
    RPC_WRONG_PARAM_SET = -1,
    RPC_WRONG_PARAM_VALUE = -2,
    RPC_WRONG_PORT = -3,
    RPC_WRONG_IO = -4,
    RPC_WRONG_RESP_LNGTH = -5
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