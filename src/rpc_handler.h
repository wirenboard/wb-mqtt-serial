
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

    RPCPortConfigSet set;
    std::string path;
    std::string ip;
    int port;
    std::vector<uint8_t> msg;
    std::chrono::milliseconds responseTimeout;
    std::chrono::milliseconds frameTimeout;
    std::string format;
    size_t responseSize;
};

class TRPCHandler
{
public:
    TRPCHandler(PMQTTSerialDriver serialDriver, WBMQTT::PMqttRpcServer rpcServer);

private:
    PMQTTSerialDriver serialDriver;
    WBMQTT::PMqttRpcServer rpcServer;
    TRPCPortConfig config;

    Json::Value PortLoad(const Json::Value& request);
    Json::Value LoadMetrics(const Json::Value& request);
};

class TRPCException: public std::runtime_error
{
public:
    TRPCException(const std::string& message, RPCPortHandlerResult resultCode)
        : std::runtime_error(message),
          message(message),
          resultCode(resultCode)
    {}

    RPCPortHandlerResult GetResultCode()
    {
        return resultCode;
    }

    std::string GetResultMessage()
    {
        return message;
    }

private:
    std::string message;
    RPCPortHandlerResult resultCode;
};