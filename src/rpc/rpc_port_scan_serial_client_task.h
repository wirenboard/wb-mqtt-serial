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

class TRPCPortScanSerialClientTask: public ISerialClientTask
{
public:
    TRPCPortScanSerialClientTask(const Json::Value& request,
                                 WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                 WBMQTT::TMqttRpcServer::TErrorCallback onError);

    ISerialClientTask::TRunResult Run(PPort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

private:
    PRPCPortScanRequest Request;
    std::chrono::steady_clock::time_point ExpireTime;
};

typedef std::shared_ptr<TRPCPortScanSerialClientTask> PRPCPortScanSerialClientTask;
