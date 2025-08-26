#pragma once

#include <wblib/rpc.h>

#include "serial_client.h"

class TRPCDeviceProbeSerialClientTask: public ISerialClientTask
{
public:
    TRPCDeviceProbeSerialClientTask(const Json::Value& request,
                                    WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                    WBMQTT::TMqttRpcServer::TErrorCallback onError);

    ISerialClientTask::TRunResult Run(PPort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

private:
    TSerialPortConnectionSettings SerialPortSettings;
    std::chrono::steady_clock::time_point ExpireTime;
    uint8_t SlaveId;
    std::unique_ptr<Modbus::IModbusTraits> ModbusTraits;

    WBMQTT::TMqttRpcServer::TResultCallback OnResult;
    WBMQTT::TMqttRpcServer::TErrorCallback OnError;
};
