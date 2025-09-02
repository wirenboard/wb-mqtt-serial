#include "rpc_device_probe_task.h"
#include "rpc_helpers.h"
#include "rpc_port_handler.h"
#include "rpc_port_scan_serial_client_task.h"
#include "serial_port.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

template<> bool inline WBMQTT::JSON::Is<uint8_t>(const Json::Value& value)
{
    return value.isUInt();
}

template<> inline uint8_t WBMQTT::JSON::As<uint8_t>(const Json::Value& value)
{
    return value.asUInt() & 0xFF;
}

TRPCDeviceProbeSerialClientTask::TRPCDeviceProbeSerialClientTask(const Json::Value& request,
                                                                 WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                                                 WBMQTT::TMqttRpcServer::TErrorCallback onError)
    : SerialPortSettings(ParseRPCSerialPortSettings(request)),
      ExpireTime(std::chrono::steady_clock::now() + DefaultRPCTotalTimeout),
      OnResult(onResult),
      OnError(onError)
{
    WBMQTT::JSON::Get(request, "slave_id", SlaveId);
    std::string protocol;
    WBMQTT::JSON::Get(request, "protocol", protocol);
    if (protocol == "modbus-tcp") {
        ModbusTraits = std::make_unique<Modbus::TModbusTCPTraits>();
    } else {
        ModbusTraits = std::make_unique<Modbus::TModbusRTUTraits>();
    }
}

ISerialClientTask::TRunResult TRPCDeviceProbeSerialClientTask::Run(PPort port,
                                                                   TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                                                   const std::list<PSerialDevice>& polledDevices)
{
    if (std::chrono::steady_clock::now() > ExpireTime) {
        if (OnError) {
            OnError(WBMQTT::E_RPC_REQUEST_TIMEOUT, "RPC request timeout");
        }
        return ISerialClientTask::TRunResult::OK;
    }

    try {
        if (!port->IsOpen()) {
            port->Open();
        }
        lastAccessedDevice.PrepareToAccess(*port, nullptr);
        TSerialPortSettingsGuard settingsGuard(port, SerialPortSettings);
        RpcPortScan::TRegisterReader reader(*port, *ModbusTraits, SlaveId);
        OnResult(RpcPortScan::GetDeviceDetails(reader, SlaveId, polledDevices));
    } catch (const std::exception& error) {
        if (OnError) {
            OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Port IO error: ") + error.what());
        }
    }

    return ISerialClientTask::TRunResult::OK;
}
