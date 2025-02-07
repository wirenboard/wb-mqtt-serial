#include "rpc_port_scan_serial_client_task.h"
#include "modbus_base.h"
#include "modbus_common.h"
#include "rpc_port_handler.h"
#include "rpc_port_scan_handler.h"
#include "serial_exc.h"
#include "serial_port.h"
#include "wb_registers.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    const std::string READ_FW_VERSION_ERROR_ID = "com.wb.device_manager.device.read_fw_version_error";
    const std::string READ_FW_SIGNATURE_ERROR_ID = "com.wb.device_manager.device.read_fw_signature_error";
    const std::string READ_DEVICE_SIGNATURE_ERROR_ID = "com.wb.device_manager.device.read_device_signature_error";
    const std::string READ_SERIAL_PARAMS_ERROR_ID = "com.wb.device_manager.device.read_serial_params_error";

    uint32_t GetSnFromRegister(const std::string& deviceModel, uint32_t sn)
    {
        if (WBMQTT::StringStartsWith(deviceModel, "WB-MAP")) {
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

    class TRegisterReader
    {
    public:
        TRegisterReader(TPort& port, uint8_t slaveId, uint32_t sn, ModbusExt::TModbusExtCommand modbusExtCommand)
            : Port(port),
              SlaveId(slaveId)
        {
            ModbusTraits.SetSn(sn);
            ModbusTraits.SetModbusExtCommand(modbusExtCommand);
            FrameTimeout = std::chrono::ceil<std::chrono::milliseconds>(
                port.GetSendTimeBytes(Modbus::STANDARD_FRAME_TIMEOUT_BYTES));
        }

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
        ModbusExt::TModbusTraits ModbusTraits;
        std::chrono::milliseconds FrameTimeout;
        TPort& Port;
        uint8_t SlaveId;
    };

    std::string GetDeviceSignature(TRegisterReader& reader)
    {
        try {
            return reader.Read<std::string>(WbRegisters::DEVICE_MODEL_EX_REGISTER_NAME);
        } catch (const std::exception& e) {
            return reader.Read<std::string>(WbRegisters::DEVICE_MODEL_REGISTER_NAME);
        }
    }

    Json::Value GetScannedDeviceDetails(TPort& port, const ModbusExt::TScannedDevice& scannedDevice)
    {
        TRegisterReader reader(port, scannedDevice.SlaveId, scannedDevice.Sn, scannedDevice.FastModbusCommand);
        Json::Value errorsJson(Json::arrayValue);

        Json::Value deviceJson;
        try {
            deviceJson["device_signature"] = GetDeviceSignature(reader);
            deviceJson["sn"] =
                std::to_string(GetSnFromRegister(deviceJson["device_signature"].asString(), scannedDevice.Sn));
        } catch (const std::exception& e) {
            AppendError(errorsJson, READ_DEVICE_SIGNATURE_ERROR_ID, e.what());
            deviceJson["sn"] = std::to_string(scannedDevice.Sn);
        }

        try {
            deviceJson["fw_signature"] = reader.Read<std::string>(WbRegisters::FW_SIGNATURE_REGISTER_NAME);
        } catch (const std::exception& e) {
            AppendError(errorsJson, READ_FW_SIGNATURE_ERROR_ID, e.what());
        }

        Json::Value cfgJson;
        cfgJson["slave_id"] = scannedDevice.SlaveId;
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

        Json::Value fwJson;
        try {
            fwJson["version"] = reader.Read<std::string>(WbRegisters::FW_VERSION_REGISTER_NAME);
        } catch (const std::exception& e) {
            AppendError(errorsJson, READ_FW_VERSION_ERROR_ID, e.what());
        }
        fwJson["fast_modbus_command"] = scannedDevice.FastModbusCommand;
        deviceJson["fw"] = fwJson;

        return deviceJson;
    }
}

PRPCPortScanRequest ParseRPCPortScanRequest(const Json::Value& request)
{
    PRPCPortScanRequest res = std::make_shared<TRPCPortScanRequest>();
    res->SerialPortSettings = ParseRPCSerialPortSettings(request);
    WBMQTT::JSON::Get(request, "total_timeout", res->TotalTimeout);
    return res;
}

void ExecRPCPortScanRequest(TPort& port, PRPCPortScanRequest rpcRequest)
{
    port.CheckPortOpen();
    port.SkipNoise();
    auto devices = ModbusExt::Scan(port, ModbusExt::TModbusExtCommand::ACTUAL);
    auto oldDevices = ModbusExt::Scan(port, ModbusExt::TModbusExtCommand::DEPRECATED);
    std::vector<ModbusExt::TScannedDevice> res;
    res.reserve(devices.size() + oldDevices.size());
    std::merge(devices.begin(),
               devices.end(),
               oldDevices.begin(),
               oldDevices.end(),
               std::back_inserter(res),
               [](const auto& d1, const auto& d2) {
                   if (d1.Sn == d2.Sn) {
                       return d1.FastModbusCommand < d2.FastModbusCommand;
                   }
                   return d1.Sn < d2.Sn;
               });
    res.erase(std::unique(res.begin(), res.end(), [](const auto& d1, const auto& d2) { return d1.Sn == d2.Sn; }),
              res.end());
    if (rpcRequest->OnResult) {
        Json::Value replyJSON;
        replyJSON["devices"] = Json::Value(Json::arrayValue);
        for (const auto& device: res) {
            try {
                replyJSON["devices"].append(GetScannedDeviceDetails(port, device));
            } catch (const std::exception& e) {
                LOG(Warn) << "Port " << port.GetDescription()
                          << " scan. Failed to get details for device (sn: " << device.Sn
                          << ", address: " << device.SlaveId << "):" << e.what();
            }
        }
        rpcRequest->OnResult(replyJSON);
    }
}

TRPCPortScanSerialClientTask::TRPCPortScanSerialClientTask(PRPCPortScanRequest request): Request(request)
{
    Request = request;
    ExpireTime = std::chrono::steady_clock::now() + Request->TotalTimeout;
}

ISerialClientTask::TRunResult TRPCPortScanSerialClientTask::Run(PPort port,
                                                                TSerialClientDeviceAccessHandler& lastAccessedDevice)
{
    if (std::chrono::steady_clock::now() > ExpireTime) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_REQUEST_TIMEOUT, "RPC request timeout");
        }
        return ISerialClientTask::TRunResult::OK;
    }

    try {
        lastAccessedDevice.PrepareToAccess(nullptr);
        TSerialPortSettingsGuard settingsGuard(port, Request->SerialPortSettings);
        ExecRPCPortScanRequest(*port, Request);
    } catch (const std::exception& error) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Port IO error: ") + error.what());
        }
    }
    return ISerialClientTask::TRunResult::OK;
}
