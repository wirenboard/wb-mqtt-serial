#pragma once
#include "rpc_config.h"
#include "serial_driver.h"
#include "serial_port_driver.h"
#include <wblib/json_utils.h>
#include <wblib/rpc.h>

const std::chrono::seconds DefaultRPCTotalTimeout(10);

// RPC Request execution result code
enum class TRPCResultCode
{
    // No errors
    RPC_OK = 0,
    // Wrong parameters value
    RPC_WRONG_PARAM_VALUE = -1,
    // Requested port was not found
    RPC_WRONG_PORT = -2,
    // Unsuccessful port IO
    RPC_WRONG_IO = -3,
    // RPC request handling timeout
    RPC_WRONG_TIMEOUT = -4
};

struct TRPCOptions
{
    PRPCRequest RPCRequest;
    PRPCPort RPCPort;
};

typedef std::shared_ptr<TRPCOptions> PRPCOptions;

class TRPCPortDriver
{
public:
    PSerialPortDriver SerialPortDriver;
    PRPCPort RPCPort;
    std::vector<uint8_t> SendRequest(PRPCRequest Request);
};

typedef std::shared_ptr<TRPCPortDriver> PRPCPortDriver;

class TRPCHandler
{
public:
    TRPCHandler(PRPCConfig RpcConfig, WBMQTT::PMqttRpcServer RpcServer, PMQTTSerialDriver SerialDriver);

private:
    Json::Value RequestSchema;
    PMQTTSerialDriver SerialDriver;
    std::vector<PRPCPortDriver> PortDrivers;

    PRPCPortDriver FindPortDriver(PRPCPort RequestedPort);

    Json::Value PortLoad(const Json::Value& Request);
    Json::Value LoadMetrics(const Json::Value& Request);
};

typedef std::shared_ptr<TRPCHandler> PRPCHandler;

namespace RPC_REQUEST
{
    void Handle(PRPCQueueMessage Message);
}

class TRPCException: public std::runtime_error
{
public:
    TRPCException(const std::string& message, TRPCResultCode resultCode);
    TRPCResultCode GetResultCode();
    std::string GetResultMessage();

private:
    std::string Message;
    TRPCResultCode ResultCode;
};