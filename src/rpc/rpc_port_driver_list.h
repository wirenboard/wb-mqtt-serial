#pragma once
#include "rpc_config.h"
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

class TRPCException: public std::runtime_error
{
public:
    TRPCException(const std::string& message, TRPCResultCode resultCode);
    TRPCResultCode GetResultCode() const;
    std::string GetResultMessage() const;

private:
    TRPCResultCode ResultCode;
};

class TRPCPortDriverList
{
public:
    TRPCPortDriverList(PRPCConfig rpcConfig, PMQTTSerialDriver serialDriver);
    PRPCPortDriver Find(const Json::Value& request) const;
    PPort InitPort(const Json::Value& request);
    void ProcessException(const TRPCException& e, WBMQTT::TMqttRpcServer::TErrorCallback onError);

private:
    std::vector<PRPCPortDriver> Drivers;
};

typedef std::shared_ptr<TRPCPortDriverList> PRPCPortDriverList;

TSerialPortConnectionSettings ParseRPCSerialPortSettings(const Json::Value& request);
