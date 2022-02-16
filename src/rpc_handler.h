
#include <serial_driver.h>
#include <wblib/rpc.h>

enum RPCPortLoadResult
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
    PMQTTSerialDriver serialDriver;
    WBMQTT::PMqttRpcServer rpcServer;

    Json::Value PortLoad(const Json::Value& request);
};

class TRPCException: public std::runtime_error
{
public:
    TRPCException(const std::string& message, enum RPCPortLoadResult resultCode)
        : std::runtime_error(message),
          message(message),
          resultCode(resultCode)
    {}

    enum RPCPortLoadResult GetResultCode()
    {
        return resultCode;
    }

    std::string GetResultMessage()
    {
        return message;
    }

private:
    std::string message;
    enum RPCPortLoadResult resultCode;
};