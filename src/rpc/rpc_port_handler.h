#pragma once
#include "rpc_config.h"
#include "rpc_port_load_request.h"
#include "serial_driver.h"
#include "serial_port_driver.h"
#include <wblib/json_utils.h>
#include <wblib/rpc.h>

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

struct TRPCPortDriver
{
    PSerialClient SerialClient;
    PRPCPort RPCPort;
};

typedef std::shared_ptr<TRPCPortDriver> PRPCPortDriver;

class TRPCPortHandler
{
public:
    TRPCPortHandler(const std::string& requestPortLoadSchemaFilePath,
                    const std::string& requestPortSetupSchemaFilePath,
                    PRPCConfig rpcConfig,
                    WBMQTT::PMqttRpcServer rpcServer,
                    PMQTTSerialDriver serialDriver);

private:
    Json::Value RequestPortLoadSchema;
    Json::Value RequestPortSetupSchema;
    PMQTTSerialDriver SerialDriver;
    std::vector<PRPCPortDriver> PortDrivers;
    PRPCConfig RPCConfig;

    PRPCPortDriver FindPortDriver(const Json::Value& request) const;

    void PortLoad(const Json::Value& request,
                  WBMQTT::TMqttRpcServer::TResultCallback onResult,
                  WBMQTT::TMqttRpcServer::TErrorCallback onError);
    void PortSetup(const Json::Value& request,
                   WBMQTT::TMqttRpcServer::TResultCallback onResult,
                   WBMQTT::TMqttRpcServer::TErrorCallback onError);
    Json::Value LoadPorts(const Json::Value& request);
};

typedef std::shared_ptr<TRPCPortHandler> PRPCPortHandler;

class TRPCException: public std::runtime_error
{
public:
    TRPCException(const std::string& message, TRPCResultCode resultCode);
    TRPCResultCode GetResultCode() const;
    std::string GetResultMessage() const;

private:
    TRPCResultCode ResultCode;
};