#include "rpc_port_setup_handler.h"
#include "rpc_port_handler.h"
#include "rpc_port_load_handler.h"
#include "rpc_port_setup_serial_client_task.h"
#include "serial_port.h"
#include "tcp_port.h"

namespace
{
    const auto WB_BAUD_RATE_REGISTER_ADDRESS = 110;
    const auto WB_PARITY_REGISTER_ADDRESS = 111;
    const auto WB_STOP_BITS_REGISTER_ADDRESS = 112;
    const auto WB_SLAVE_ID_REGISTER_ADDRESS = 128;

    TRPCPortSetupRequestItem ParseRPCPortSetupItemRequest(const Json::Value& request)
    {
        TRPCPortSetupRequestItem res;

        res.SlaveId = request["slave_id"].asUInt();

        if (request.isMember("sn")) {
            res.Sn = request["sn"].asUInt();
        }

        std::unordered_map<std::string, PRegisterConfig> regConfigs;
        regConfigs["baud_rate"] = TRegisterConfig::Create(
            Modbus::REG_HOLDING,
            TRegisterDesc{std::make_shared<TUint32RegisterAddress>(WB_BAUD_RATE_REGISTER_ADDRESS), 0, 0},
            U16,
            100);
        regConfigs["parity"] = TRegisterConfig::Create(
            Modbus::REG_HOLDING,
            TRegisterDesc{std::make_shared<TUint32RegisterAddress>(WB_PARITY_REGISTER_ADDRESS), 0, 0});
        regConfigs["stop_bits"] = TRegisterConfig::Create(
            Modbus::REG_HOLDING,
            TRegisterDesc{std::make_shared<TUint32RegisterAddress>(WB_STOP_BITS_REGISTER_ADDRESS), 0, 0});
        regConfigs["slave_id"] = TRegisterConfig::Create(
            Modbus::REG_HOLDING,
            TRegisterDesc{std::make_shared<TUint32RegisterAddress>(WB_SLAVE_ID_REGISTER_ADDRESS), 0, 0});

        if (request.isMember("cfg")) {
            for (auto& item: regConfigs) {
                if (request["cfg"].isMember(item.first)) {
                    PDeviceSetupItemConfig setup_item_config(
                        std::make_shared<TDeviceSetupItemConfig>(item.first,
                                                                 item.second,
                                                                 request["cfg"][item.first].asString()));
                    res.Regs.push_back(
                        std::make_shared<TDeviceSetupItem>(setup_item_config,
                                                           std::make_shared<TRegister>(nullptr, item.second)));
                }
            }
        }

        res.SerialPortSettings = ParseRPCSerialPortSettings(request);

        return res;
    }

} // namespace

PRPCPortSetupRequest ParseRPCPortSetupRequest(const Json::Value& requestJson, const Json::Value& requestSchema)
{
    WBMQTT::JSON::Validate(requestJson, requestSchema);

    auto RPCRequest = std::make_shared<TRPCPortSetupRequest>();

    try {
        for (auto& request: requestJson["items"]) {
            RPCRequest->Items.push_back(ParseRPCPortSetupItemRequest(request));
        }

        if (!WBMQTT::JSON::Get(requestJson, "total_timeout", RPCRequest->TotalTimeout)) {
            RPCRequest->TotalTimeout = DefaultRPCTotalTimeout;
        }
    } catch (const std::runtime_error& e) {
        throw TRPCException(e.what(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }

    return RPCRequest;
}

void RPCPortSetupHandler(PRPCPortSetupRequest rpcRequest,
                         PSerialClient serialClient,
                         WBMQTT::TMqttRpcServer::TResultCallback onResult,
                         WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    rpcRequest->OnResult = [onResult, rpcRequest]() {
        Json::Value replyJSON;
        onResult(replyJSON);
    };
    rpcRequest->OnError = onError;
    if (serialClient) {
        auto task(std::make_shared<TRPCPortSetupSerialClientTask>(rpcRequest));
        serialClient->AddTask(task);
    } else {
        throw TRPCException("SerialClient wasn't found for requested port", TRPCResultCode::RPC_WRONG_PORT);
    }
}

void RPCPortSetupHandler(PRPCPortSetupRequest rpcRequest,
                         PPort port,
                         WBMQTT::TMqttRpcServer::TResultCallback onResult,
                         WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    Modbus::TModbusRTUTraitsFactory traitsFactory;
    auto rtuTraits = traitsFactory.GetModbusTraits(port, false);
    ModbusExt::TModbusTraits fastModbusTraits;
    Modbus::TRegisterCache cache;
    auto frameTimeout =
        std::chrono::ceil<std::chrono::milliseconds>(port->GetSendTimeBytes(Modbus::STANDARD_FRAME_TIMEOUT_BYTES));
    port->Open();
    for (auto item: rpcRequest->Items) {
        TSerialPortSettingsGuard settingsGuard(port, item.SerialPortSettings);
        port->SleepSinceLastInteraction(frameTimeout);
        Modbus::WriteSetupRegisters(item.Sn ? fastModbusTraits : *rtuTraits,
                                    *port,
                                    item.SlaveId,
                                    item.Sn.value_or(0),
                                    item.Regs,
                                    cache,
                                    std::chrono::microseconds(0),
                                    std::chrono::milliseconds(1),
                                    frameTimeout);
    }
    Json::Value replyJSON;
    onResult(replyJSON);
}
