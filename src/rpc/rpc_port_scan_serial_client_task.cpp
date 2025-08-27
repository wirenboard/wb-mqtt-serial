#include "rpc_port_scan_serial_client_task.h"
#include "modbus_base.h"
#include "modbus_common.h"
#include "rpc_helpers.h"
#include "serial_port.h"
#include "wb_registers.h"
#include <regex>

#define LOG(logger) ::logger.Log() << "[RPC] "

template<> bool inline WBMQTT::JSON::Is<ModbusExt::TModbusExtCommand>(const Json::Value& value)
{
    return value.isUInt();
}

template<> inline ModbusExt::TModbusExtCommand WBMQTT::JSON::As<ModbusExt::TModbusExtCommand>(const Json::Value& value)
{
    return ModbusExt::TModbusExtCommand(value.asUInt());
}

namespace
{
    const std::string READ_FW_VERSION_ERROR_ID = "com.wb.device_manager.device.read_fw_version_error";
    const std::string READ_FW_SIGNATURE_ERROR_ID = "com.wb.device_manager.device.read_fw_signature_error";
    const std::string READ_DEVICE_SIGNATURE_ERROR_ID = "com.wb.device_manager.device.read_device_signature_error";
    const std::string READ_SERIAL_PARAMS_ERROR_ID = "com.wb.device_manager.device.read_serial_params_error";
    const std::string READ_SN_ERROR_ID = "com.wb.device_manager.device.read_sn_error";

    uint32_t GetSnFromRegister(const std::string& deviceModel, uint32_t sn)
    {
        if (std::regex_match(deviceModel, std::regex("^MAP[0-9]{1,2}.*"))) {
            return sn & 0x00FFFFFF;
        }
        return sn;
    }

    std::string GetParityChar(uint64_t parity)
    {
        const std::unordered_map<uint64_t, std::string> parityMap = {{0, "N"}, {1, "O"}, {2, "E"}};
        auto it = parityMap.find(parity);
        return (it != parityMap.end() ? it->second : "U");
    }

    void AppendError(Json::Value& errorsJson, const std::string& id, const std::string& message)
    {
        Json::Value errorJson;
        errorJson["id"] = id;
        errorJson["message"] = message;
        errorsJson.append(errorJson);
    }

    std::string GetDeviceSignature(RpcPortScan::TRegisterReader& reader)
    {
        try {
            return reader.Read<std::string>(WbRegisters::DEVICE_MODEL_EX_REGISTER_NAME);
        } catch (const std::exception& e) {
            return reader.Read<std::string>(WbRegisters::DEVICE_MODEL_REGISTER_NAME);
        }
    }

    Json::Value GetDeviceDetails(RpcPortScan::TRegisterReader& reader,
                                 uint8_t slaveId,
                                 uint32_t snFromRegister,
                                 const std::list<PSerialDevice>& polledDevices)
    {
        Json::Value errorsJson(Json::arrayValue);
        Json::Value deviceJson;

        std::string stringSn = std::to_string(snFromRegister);
        try {
            deviceJson["device_signature"] = GetDeviceSignature(reader);
            stringSn = std::to_string(GetSnFromRegister(deviceJson["device_signature"].asString(), snFromRegister));
        } catch (const std::exception& e) {
            AppendError(errorsJson, READ_DEVICE_SIGNATURE_ERROR_ID, e.what());
        }
        deviceJson["sn"] = stringSn;

        try {
            deviceJson["fw_signature"] = reader.Read<std::string>(WbRegisters::FW_SIGNATURE_REGISTER_NAME);
        } catch (const std::exception& e) {
            AppendError(errorsJson, READ_FW_SIGNATURE_ERROR_ID, e.what());
        }

        Json::Value cfgJson;
        cfgJson["slave_id"] = slaveId;
        cfgJson["data_bits"] = 8;
        std::string serialParamsReadError;
        try {
            cfgJson["baud_rate"] = reader.Read<uint64_t>(WbRegisters::BAUD_RATE_REGISTER_NAME) * 100;
        } catch (const std::exception& e) {
            serialParamsReadError = e.what();
        }
        try {
            cfgJson["stop_bits"] = reader.Read<uint64_t>(WbRegisters::STOP_BITS_REGISTER_NAME);
        } catch (const std::exception& e) {
            serialParamsReadError = e.what();
        }
        try {
            cfgJson["parity"] = GetParityChar(reader.Read<uint64_t>(WbRegisters::PARITY_REGISTER_NAME));
        } catch (const std::exception& e) {
            serialParamsReadError = e.what();
        }
        if (!serialParamsReadError.empty()) {
            AppendError(errorsJson, READ_SERIAL_PARAMS_ERROR_ID, serialParamsReadError);
        }
        deviceJson["cfg"] = cfgJson;

        try {
            Json::Value fwJson;
            fwJson["version"] = reader.Read<std::string>(WbRegisters::FW_VERSION_REGISTER_NAME);
            deviceJson["fw"] = fwJson;
        } catch (const std::exception& e) {
            AppendError(errorsJson, READ_FW_VERSION_ERROR_ID, e.what());
        }

        auto stringSlaveId = std::to_string(slaveId);
        for (const auto& polledDevice: polledDevices) {
            if (polledDevice->DeviceConfig()->SlaveId == stringSlaveId && polledDevice->Protocol()->IsModbus()) {
                auto snRegister = polledDevice->GetSnRegister();
                if (snRegister) {
                    if (snRegister->GetErrorState().test(TRegister::ReadError) ||
                        snRegister->GetValue().GetType() == TRegisterValue::ValueType::Undefined)
                    {
                        auto registerRange = polledDevice->CreateRegisterRange();
                        registerRange->Add(reader.GetPort(), snRegister, std::chrono::milliseconds::max());
                        polledDevice->ReadRegisterRange(reader.GetPort(), registerRange);
                    }
                    if (snRegister->GetValue().GetType() == TRegisterValue::ValueType::Integer &&
                        snFromRegister == snRegister->GetValue().Get<uint64_t>())
                    {
                        deviceJson["configured_device_type"] = polledDevice->DeviceConfig()->DeviceType;
                        break;
                    }
                }
            }
        }

        if (!errorsJson.empty()) {
            deviceJson["errors"] = errorsJson;
        }

        return deviceJson;
    }

    Json::Value GetScannedDeviceDetails(TPort& port,
                                        const ModbusExt::TScannedDevice& scannedDevice,
                                        ModbusExt::TModbusExtCommand modbusExtCommand,
                                        const std::list<PSerialDevice>& polledDevices)
    {
        ModbusExt::TModbusTraits modbusTraits;
        modbusTraits.SetModbusExtCommand(modbusExtCommand);
        modbusTraits.SetSn(scannedDevice.Sn);
        RpcPortScan::TRegisterReader reader(port, modbusTraits, scannedDevice.SlaveId);
        return ::GetDeviceDetails(reader, scannedDevice.SlaveId, scannedDevice.Sn, polledDevices);
    }
}

Json::Value RpcPortScan::GetDeviceDetails(RpcPortScan::TRegisterReader& reader,
                                          uint8_t slaveId,
                                          const std::list<PSerialDevice>& polledDevices)
{
    uint64_t sn;
    try {
        sn = reader.Read<uint64_t>(WbRegisters::SN_REGISTER_NAME);
    } catch (const TResponseTimeoutException& e) {
        return Json::Value(Json::objectValue);
    } catch (const Modbus::TErrorBase& e) {
        Json::Value errorsJson(Json::arrayValue);
        AppendError(errorsJson, READ_SN_ERROR_ID, e.what());
        Json::Value deviceJson;
        deviceJson["errors"] = errorsJson;
        return deviceJson;
    }
    return ::GetDeviceDetails(reader, slaveId, sn, polledDevices);
}

RpcPortScan::TRegisterReader::TRegisterReader(TPort& port, Modbus::IModbusTraits& modbusTraits, uint8_t slaveId)
    : ModbusTraits(modbusTraits),
      FrameTimeout(
          std::chrono::ceil<std::chrono::milliseconds>(port.GetSendTimeBytes(Modbus::STANDARD_FRAME_TIMEOUT_BYTES))),
      Port(port),
      SlaveId(slaveId)
{}

PRPCPortScanRequest ParseRPCPortScanRequest(const Json::Value& request)
{
    PRPCPortScanRequest res = std::make_shared<TRPCPortScanRequest>();
    res->SerialPortSettings = ParseRPCSerialPortSettings(request);
    WBMQTT::JSON::Get(request, "total_timeout", res->TotalTimeout);
    WBMQTT::JSON::Get(request, "command", res->ModbusExtCommand);
    return res;
}

void ExecRPCPortScanRequest(TPort& port, PRPCPortScanRequest rpcRequest, const std::list<PSerialDevice>& polledDevices)
{
    if (!rpcRequest->OnResult) {
        return;
    }

    port.SkipNoise();

    Json::Value replyJSON;
    std::vector<ModbusExt::TScannedDevice> scannedDevices;
    try {
        switch (rpcRequest->Mode) {
            case TRPCPortScanRequest::START: {
                auto device = ModbusExt::ScanStart(port, rpcRequest->ModbusExtCommand);
                if (device) {
                    scannedDevices.push_back(*device);
                }
                break;
            }
            case TRPCPortScanRequest::NEXT: {
                auto device = ModbusExt::ScanNext(port, rpcRequest->ModbusExtCommand);
                if (device) {
                    scannedDevices.push_back(*device);
                }
                break;
            }
            case TRPCPortScanRequest::ALL: {
                ModbusExt::Scan(port, rpcRequest->ModbusExtCommand, scannedDevices);
                break;
            }
        }
    } catch (const std::exception& e) {
        replyJSON["error"] = e.what();
    }

    replyJSON["devices"] = Json::Value(Json::arrayValue);
    for (const auto& device: scannedDevices) {
        replyJSON["devices"].append(GetScannedDeviceDetails(port, device, rpcRequest->ModbusExtCommand, polledDevices));
    }

    rpcRequest->OnResult(replyJSON);
}

TRPCPortScanSerialClientTask::TRPCPortScanSerialClientTask(const Json::Value& request,
                                                           WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                                           WBMQTT::TMqttRpcServer::TErrorCallback onError)
    : Request(ParseRPCPortScanRequest(request))
{
    Request->OnResult = onResult;
    Request->OnError = onError;
    std::string mode;
    WBMQTT::JSON::Get(request, "mode", mode);
    if (mode == "start") {
        Request->Mode = TRPCPortScanRequest::START;
    } else if (mode == "next") {
        Request->Mode = TRPCPortScanRequest::NEXT;
    }
    ExpireTime = std::chrono::steady_clock::now() + Request->TotalTimeout;
}

ISerialClientTask::TRunResult TRPCPortScanSerialClientTask::Run(PPort port,
                                                                TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                                                const std::list<PSerialDevice>& polledDevices)
{
    if (std::chrono::steady_clock::now() > ExpireTime) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_REQUEST_TIMEOUT, "RPC request timeout");
        }
        return ISerialClientTask::TRunResult::OK;
    }

    try {
        if (!port->IsOpen()) {
            port->Open();
        }
        lastAccessedDevice.PrepareToAccess(*port, nullptr);
        TSerialPortSettingsGuard settingsGuard(port, Request->SerialPortSettings);
        ExecRPCPortScanRequest(*port, Request, polledDevices);
    } catch (const std::exception& error) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Port IO error: ") + error.what());
        }
    }
    return ISerialClientTask::TRunResult::OK;
}
