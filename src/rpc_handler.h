#pragma once
#include "rpc_config.h"
#include "serial_driver.h"
#include "serial_port_driver.h"
#include <wblib/json_utils.h>
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

class TRPCPortDriver: public TRPCPort
{
public:
    PSerialPortDriver SerialPortDriver;
};

typedef std::shared_ptr<TRPCPortDriver> PRPCPortDriver;

class TRPCHandler
{
public:
    void RPCServerInitialize(WBMQTT::PMqttRpcServer RpcServer);
    void SerialDriverInitialize(PMQTTSerialDriver SerialDriver);
    void RPCConfigInitialize(PRPCConfig rpcConfig);
    void AddSerialPortDriver(PSerialPortDriver SerialPortDriver, PPort Port);

private:
    PSerialPortDriver FindSerialPortDriverByPath(enum TRPCPortMode Mode,
                                                 std::string Path,
                                                 std::string Ip,
                                                 int PortNumber);

    Json::Value PortLoad(const Json::Value& Request);
    Json::Value LoadMetrics(const Json::Value& Request);

    std::vector<PRPCPortDriver> PortDrivers;
    PMQTTSerialDriver SerialDriver;
    PRPCConfig Config;
};

typedef std::shared_ptr<TRPCHandler> PRPCHandler;

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