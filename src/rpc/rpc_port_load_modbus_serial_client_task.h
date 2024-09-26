#pragma once

#include <chrono>
#include <wblib/rpc.h>

#include "rpc_port_load_request.h"
#include "serial_client.h"

class TRPCPortLoadModbusRequest: public TRPCPortLoadRequest
{
public:
    uint8_t SlaveId;
    uint16_t Address;
    size_t Count;
    Modbus::EFunction Function;
};

typedef std::shared_ptr<TRPCPortLoadModbusRequest> PRPCPortLoadModbusRequest;

PRPCPortLoadModbusRequest ParseRPCPortLoadModbusRequest(const Json::Value& request);

class TRPCPortLoadModbusSerialClientTask: public ISerialClientTask
{
public:
    TRPCPortLoadModbusSerialClientTask(PRPCPortLoadModbusRequest request);

    ISerialClientTask::TRunResult Run(PPort port, TSerialClientDeviceAccessHandler& lastAccessedDevice) override;

private:
    PRPCPortLoadModbusRequest Request;
    std::chrono::steady_clock::time_point ExpireTime;
};

typedef std::shared_ptr<TRPCPortLoadModbusSerialClientTask> PRPCPortLoadModbusSerialClientTask;

void ExecRPCPortLoadModbusRequest(TPort& port, PRPCPortLoadModbusRequest rpcRequest);
