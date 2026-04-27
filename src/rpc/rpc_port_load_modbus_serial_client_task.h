#pragma once

#include <chrono>
#include <wblib/rpc.h>

#include "rpc_port_load_request.h"
#include "serial_client.h"

class TRPCPortLoadModbusRequest: public TRPCPortLoadRequestBase
{
public:
    TRPCPortLoadModbusRequest(TRPCDeviceParametersCache& parametersCache);
    TRPCDeviceParametersCache& ParametersCache;
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

private:
    PRPCPortLoadModbusRequest Request;
    std::chrono::steady_clock::time_point ExpireTime;
};

PRPCPortLoadModbusRequest ParseRPCPortLoadModbusRequest(const Json::Value& request,
                                                        TRPCDeviceParametersCache& parametersCache);

typedef std::shared_ptr<TRPCPortLoadModbusSerialClientTask> PRPCPortLoadModbusSerialClientTask;
