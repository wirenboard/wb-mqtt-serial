#pragma once

#include <chrono>
#include <wblib/rpc.h>

#include "rpc_port_handler.h"
#include "serial_client.h"
#include "wb_registers.h"

namespace RpcPortScan
{
    class TRegisterReader
    {
    public:
        TRegisterReader(TPort& port, Modbus::IModbusTraits& modbusTraits, uint8_t slaveId);

        template<typename T> T Read(const std::string& registerName)
        {
            auto registerConfig = WbRegisters::GetRegisterConfig(registerName);
            if (!registerConfig) {
                throw std::runtime_error("Unknown register name: " + registerName);
            }
            auto res = Modbus::ReadRegister(ModbusTraits,
                                            Port,
                                            SlaveId,
                                            *registerConfig,
                                            FrameTimeout,
                                            FrameTimeout,
                                            FrameTimeout)
                           .Get<T>();
            return res;
        }

    private:
        Modbus::IModbusTraits& ModbusTraits;
        std::chrono::milliseconds FrameTimeout;
        TPort& Port;
        uint8_t SlaveId;
    };

    Json::Value GetDeviceDetails(TRegisterReader& reader,
                                 uint8_t slaveId,
                                 const std::list<PSerialDevice>& polledDevices);
}

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
