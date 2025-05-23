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

    /**
     * @brief Retrieves detailed information about a device.
     *
     * This function gathers details about a specific device by reading its registers
     * and analyzing its properties based on the provided slave ID and the list of
     * polled devices.
     *
     * @param reader A reference to the TRegisterReader object used to read device registers.
     * @param slaveId The Modbus slave ID of the device to retrieve details for.
     * @param polledDevices A list of polled serial devices to assist in identifying the device.
     * @return A Json::Value object containing the detailed information about the device.
     *         An empty Json::Value object is returned if the device is not found.
     *         The returned object may contain error messages if any issues occurred during
     *         the reading process.
     */
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
