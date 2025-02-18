#pragma once

#include <chrono>
#include <wblib/rpc.h>

#include "rpc_port_handler.h"
#include "serial_client.h"

class TRPCPortScanRequest
{
public:
    TSerialPortConnectionSettings SerialPortSettings;
    ModbusExt::TModbusExtCommand ModbusExtCommand = ModbusExt::TModbusExtCommand::ACTUAL;
    std::chrono::milliseconds TotalTimeout = DefaultRPCTotalTimeout;

    WBMQTT::TMqttRpcServer::TResultCallback OnResult = nullptr;
    WBMQTT::TMqttRpcServer::TErrorCallback OnError = nullptr;
};

typedef std::shared_ptr<TRPCPortScanRequest> PRPCPortScanRequest;

PRPCPortScanRequest ParseRPCPortScanRequest(const Json::Value& request);

class TRPCPortScanSerialClientTask: public ISerialClientTask
{
public:
    TRPCPortScanSerialClientTask(PRPCPortScanRequest request);

    ISerialClientTask::TRunResult Run(PPort port, TSerialClientDeviceAccessHandler& lastAccessedDevice) override;

private:
    PRPCPortScanRequest Request;
    std::chrono::steady_clock::time_point ExpireTime;
};

typedef std::shared_ptr<TRPCPortScanSerialClientTask> PRPCPortScanSerialClientTask;

void ExecRPCPortScanRequest(TPort& port, PRPCPortScanRequest rpcRequest);
