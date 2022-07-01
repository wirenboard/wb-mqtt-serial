#pragma once
#include <serial_driver.h>
#include <serial_port_driver.h>
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

class TRPCPort
{
public:
    PPort Port;
    PSerialPortDriver SerialPortDriver;
    enum TRPCModbusMode Mode;
    std::string Path, Ip;
    int PortNumber;
};

class TRPCHandler
{
public:
    void AddPort(PPort Port, enum TRPCModbusMode Mode, std::string Path, std::string Ip, int PortNumber);
    void AddSerialPortDriver(PSerialPortDriver SerialPortDriver, PPort Port);
    void RPCServerInitialize(WBMQTT::PMqttRpcServer RpcServer);
    void SerialDriverInitialize(PMQTTSerialDriver SerialDriver);

private:
    bool FindSerialDriverByPath(enum TRPCModbusMode Mode,
                                std::string Path,
                                std::string Ip,
                                int PortNumber,
                                PSerialPortDriver SerialPortDriver);

    Json::Value PortLoad(const Json::Value& request);
    Json::Value LoadMetrics(const Json::Value& request);

    std::vector<TRPCPort> Ports;
    PMQTTSerialDriver SerialDriver;
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