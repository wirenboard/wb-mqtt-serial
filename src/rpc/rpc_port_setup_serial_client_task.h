#pragma once

#include "rpc_port_handler.h"
#include "serial_client.h"
#include <chrono>
#include <wblib/rpc.h>

class TRPCPortSetupRequestItem
{
public:
    uint8_t SlaveId;
    std::optional<uint32_t> Sn;
    TSerialPortConnectionSettings SerialPortSettings;
    TDeviceSetupItems Regs;
};

class TRPCPortSetupRequest
{
public:
    std::vector<TRPCPortSetupRequestItem> Items;

    std::chrono::milliseconds TotalTimeout = DefaultRPCTotalTimeout;

    WBMQTT::TMqttRpcServer::TResultCallback OnResult = nullptr;
    WBMQTT::TMqttRpcServer::TErrorCallback OnError = nullptr;
};

typedef std::shared_ptr<TRPCPortSetupRequest> PRPCPortSetupRequest;

class TRPCPortSetupSerialClientTask: public ISerialClientTask
{
public:
    TRPCPortSetupSerialClientTask(const Json::Value& request,
                                  WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                  WBMQTT::TMqttRpcServer::TErrorCallback onError);

    ISerialClientTask::TRunResult Run(PPort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

private:
    PRPCPortSetupRequest Request;
    std::chrono::steady_clock::time_point ExpireTime;
};

typedef std::shared_ptr<TRPCPortSetupSerialClientTask> PRPCPortSetupSerialClientTask;
