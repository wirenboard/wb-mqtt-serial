#include "rpc_port_setup_handler.h"
#include "rpc_port_handler.h"
#include "rpc_port_setup_serial_client_task.h"
#include "serial_port.h"
#include "tcp_port.h"
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
                                                   WbRegisters::SLAVE_ID_REGISTER_NAME,
                                                   WbRegisters::SLAVE_ID_REGISTER_NAME};
        for (auto& regName: regNames) {
            if (request["cfg"].isMember(regName)) {
                auto regConfig = WbRegisters::GetRegisterConfig(regName);
                if (regConfig) {
                    PDeviceSetupItemConfig setup_item_config(
                        std::make_shared<TDeviceSetupItemConfig>(regName,
                                                                 regConfig,
                                                                 request["cfg"][regName].asString()));
                    res.Regs.push_back(
                        std::make_shared<TDeviceSetupItem>(setup_item_config,
                                                           std::make_shared<TRegister>(nullptr, regConfig)));
                }
            }
        }

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

        WBMQTT::JSON::Get(requestJson, "total_timeout", RPCRequest->TotalTimeout);
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
                         TPort& port,
                         WBMQTT::TMqttRpcServer::TResultCallback onResult,
                         WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    ModbusExt::TModbusTraits fastModbusTraits;
    Modbus::TModbusRTUTraits rtuTraits(false);
    auto frameTimeout =
        std::chrono::ceil<std::chrono::milliseconds>(port.GetSendTimeBytes(Modbus::STANDARD_FRAME_TIMEOUT_BYTES));
    for (auto item: rpcRequest->Items) {
        port.ApplySerialPortSettings(item.SerialPortSettings);
        port.SleepSinceLastInteraction(frameTimeout);
        if (item.Sn) {
            fastModbusTraits.SetSn(item.Sn.value());
        }
        Modbus::TRegisterCache cache;
        Modbus::WriteSetupRegisters(item.Sn ? static_cast<Modbus::IModbusTraits&>(fastModbusTraits)
                                            : static_cast<Modbus::IModbusTraits&>(rtuTraits),
                                    port,
                                    item.SlaveId,
                                    item.Regs,
                                    cache,
                                    std::chrono::microseconds(0),
                                    std::chrono::milliseconds(1),
                                    frameTimeout);
    }
    Json::Value replyJSON;
    onResult(replyJSON);
}
