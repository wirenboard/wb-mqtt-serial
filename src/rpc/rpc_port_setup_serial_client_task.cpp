#include "rpc_port_setup_serial_client_task.h"
#include "modbus_common.h"
#include "rpc_helpers.h"
#include "serial_exc.h"
#include "serial_port.h"
#include "wb_registers.h"

namespace
{
    TRPCPortSetupRequestItem ParseRPCPortSetupItemRequest(const Json::Value& request)
    {
        TRPCPortSetupRequestItem res;

        res.SlaveId = request["slave_id"].asUInt();

        if (request.isMember("sn")) {
            res.Sn = request["sn"].asUInt();
        }

        res.SerialPortSettings = ParseRPCSerialPortSettings(request);

        if (!request.isMember("cfg")) {
            return res;
        }

        const std::vector<std::string> regNames = {WbRegisters::BAUD_RATE_REGISTER_NAME,
                                                   WbRegisters::PARITY_REGISTER_NAME,
                                                   WbRegisters::STOP_BITS_REGISTER_NAME,
                                                   WbRegisters::SLAVE_ID_REGISTER_NAME};
        for (auto& regName: regNames) {
            if (request["cfg"].isMember(regName)) {
                auto regConfig = WbRegisters::GetRegisterConfig(regName);
                if (regConfig) {
                    PDeviceSetupItemConfig setup_item_config(
                        std::make_shared<TDeviceSetupItemConfig>(regName,
                                                                 regConfig,
                                                                 request["cfg"][regName].asString()));
                    res.Regs.insert(std::make_shared<TDeviceSetupItem>(setup_item_config, nullptr));
                }
            }
        }

        return res;
    }

    PRPCPortSetupRequest ParseRPCPortSetupRequest(const Json::Value& requestJson)
    {
        auto RPCRequest = std::make_shared<TRPCPortSetupRequest>();

        try {
            for (auto& request: requestJson["items"]) {
                RPCRequest->Items.push_back(ParseRPCPortSetupItemRequest(request));
            }

            WBMQTT::JSON::Get(requestJson, "total_timeout", RPCRequest->TotalTimeout);
        } catch (const std::runtime_error& e) {
            throw TRPCException(e.what(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
        }

        return RPCRequest;
    }
}

TRPCPortSetupSerialClientTask::TRPCPortSetupSerialClientTask(const Json::Value& request,
                                                             WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                                             WBMQTT::TMqttRpcServer::TErrorCallback onError)
    : Request(ParseRPCPortSetupRequest(request))
{
    Request->OnResult = onResult;
    Request->OnError = onError;
    ExpireTime = std::chrono::steady_clock::now() + Request->TotalTimeout;
}

ISerialClientTask::TRunResult TRPCPortSetupSerialClientTask::Run(PPort port,
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
        port->SkipNoise();
        ModbusExt::TModbusTraits fastModbusTraits;
        Modbus::TModbusRTUTraits rtuTraits(false);
        auto frameTimeout =
            std::chrono::ceil<std::chrono::milliseconds>(port->GetSendTimeBytes(Modbus::STANDARD_FRAME_TIMEOUT_BYTES));
        for (auto item: Request->Items) {
            TSerialPortSettingsGuard settingsGuard(port, item.SerialPortSettings);
            port->SleepSinceLastInteraction(frameTimeout);
            lastAccessedDevice.PrepareToAccess(*port, nullptr);

            if (item.Sn) {
                fastModbusTraits.SetSn(item.Sn.value());
            }
            Modbus::TRegisterCache cache;
            Modbus::WriteSetupRegisters(item.Sn ? static_cast<Modbus::IModbusTraits&>(fastModbusTraits)
                                                : static_cast<Modbus::IModbusTraits&>(rtuTraits),
                                        *port,
                                        item.SlaveId,
                                        item.Regs,
                                        cache,
                                        std::chrono::microseconds(0),
                                        std::chrono::milliseconds(1),
                                        frameTimeout,
                                        false);
        }

        if (Request->OnResult) {
            Json::Value replyJSON;
            Request->OnResult(replyJSON);
        }
    } catch (const std::exception& error) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Port IO error: ") + error.what());
        }
    }
    return ISerialClientTask::TRunResult::OK;
}
