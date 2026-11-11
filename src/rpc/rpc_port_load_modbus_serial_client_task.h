#pragma once

#include <chrono>
#include <wblib/rpc.h>

#include "rpc_port_load_request.h"
#include "serial_client.h"

class TRPCPortLoadModbusRequest: public TRPCPortLoadRequestBase
{
public:
    TRPCPortLoadModbusRequest(PDeviceParametersCache parametersCache);
    PDeviceParametersCache ParametersCache;
    uint8_t SlaveId;
    uint16_t Address;
    uint16_t Count = 1;
    uint16_t WriteAddress;
    uint16_t WriteCount = 1;
    Modbus::EFunction Function;
    std::string Protocol;
    std::unique_ptr<TSerialPortConnectionSettings> SerialPortSettings;
};

typedef std::shared_ptr<TRPCPortLoadModbusRequest> PRPCPortLoadModbusRequest;

class TRPCPortLoadModbusSerialClientTask: public ISerialClientTask
{
public:
    TRPCPortLoadModbusSerialClientTask(PRPCPortLoadModbusRequest request);

    ISerialClientTask::TRunResult Run(PFeaturePort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

protected:
    PRPCPortLoadModbusRequest Request;

    //! Returns false if the request must not be sent, the error is already answered then
    virtual bool PrepareDevice(TFeaturePort& port, TSerialClientDeviceAccessHandler& lastAccessedDevice);

private:
    std::chrono::steady_clock::time_point ExpireTime;
};

//! The device is set up before the request as for polling
class TRPCPortLoadConfiguredDeviceTask: public TRPCPortLoadModbusSerialClientTask
{
public:
    TRPCPortLoadConfiguredDeviceTask(PRPCPortLoadModbusRequest request, PSerialDevice device);

protected:
    bool PrepareDevice(TFeaturePort& port, TSerialClientDeviceAccessHandler& lastAccessedDevice) override;

private:
    PSerialDevice Device;
};

PRPCPortLoadModbusRequest ParseRPCPortLoadModbusRequest(const Json::Value& request,
                                                        PDeviceParametersCache parametersCache);

//! deviceFromConfig is null if the request is not to a device from the config
PSerialClientTask MakeRPCPortLoadModbusTask(const Json::Value& request,
                                            PDeviceParametersCache parametersCache,
                                            PSerialDevice deviceFromConfig,
                                            WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                            WBMQTT::TMqttRpcServer::TErrorCallback onError);

typedef std::shared_ptr<TRPCPortLoadModbusSerialClientTask> PRPCPortLoadModbusSerialClientTask;
