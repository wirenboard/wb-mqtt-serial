#include "rpc_port_load_modbus_serial_client_task.h"
#include "modbus_base.h"
#include "rpc_port_handler.h"
#include "serial_exc.h"
#include "serial_port.h"

template<> bool inline WBMQTT::JSON::Is<uint8_t>(const Json::Value& value)
{
    return value.isUInt();
}

template<> inline uint8_t WBMQTT::JSON::As<uint8_t>(const Json::Value& value)
{
    return value.asUInt() & 0xFF;
}

template<> bool inline WBMQTT::JSON::Is<uint16_t>(const Json::Value& value)
{
    return value.isUInt();
}

template<> inline uint16_t WBMQTT::JSON::As<uint16_t>(const Json::Value& value)
{
    return value.asUInt() & 0xFFFF;
}

template<> bool inline WBMQTT::JSON::Is<Modbus::EFunction>(const Json::Value& value)
{
    if (!value.isUInt()) {
        return false;
    }
    auto fun = value.asUInt();
    return Modbus::IsSupportedFunction(fun & 0xFF);
}

template<> inline Modbus::EFunction WBMQTT::JSON::As<Modbus::EFunction>(const Json::Value& value)
{
    return static_cast<Modbus::EFunction>(value.asUInt() & 0xFF);
}

TRPCPortLoadModbusRequest::TRPCPortLoadModbusRequest(TRPCDeviceParametersCache& parametersCache)
    : ParametersCache(parametersCache)
{}

PRPCPortLoadModbusRequest ParseRPCPortLoadModbusRequest(const Json::Value& request,
                                                        TRPCDeviceParametersCache& parametersCache)
{
    PRPCPortLoadModbusRequest RPCRequest = std::make_shared<TRPCPortLoadModbusRequest>(parametersCache);

    try {
        ParseRPCPortLoadRequest(request, *RPCRequest);
        WBMQTT::JSON::Get(request, "slave_id", RPCRequest->SlaveId);
        WBMQTT::JSON::Get(request, "address", RPCRequest->Address);
        WBMQTT::JSON::Get(request, "count", RPCRequest->Count);
        WBMQTT::JSON::Get(request, "function", RPCRequest->Function);
        WBMQTT::JSON::Get(request, "protocol", RPCRequest->Protocol);
    } catch (const std::runtime_error& e) {
        throw TRPCException(e.what(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }

    return RPCRequest;
}

void ExecRPCPortLoadModbusRequest(TPort& port, PRPCPortLoadModbusRequest rpcRequest)
{
    try {
        port.CheckPortOpen();
        port.SkipNoise();
        port.SleepSinceLastInteraction(rpcRequest->FrameTimeout);

        // Select traits based on protocol
        std::unique_ptr<Modbus::IModbusTraits> traits;
        if (rpcRequest->Protocol == "modbus-tcp") {
            traits = std::make_unique<Modbus::TModbusTCPTraits>();
        } else {
            traits = std::make_unique<Modbus::TModbusRTUTraits>();
        }

        auto pdu = Modbus::MakePDU(rpcRequest->Function, rpcRequest->Address, rpcRequest->Count, rpcRequest->Message);
        auto responsePduSize = Modbus::CalcResponsePDUSize(rpcRequest->Function, rpcRequest->Count);
        auto res = traits->Transaction(port,
                                      rpcRequest->SlaveId,
                                      pdu,
                                      responsePduSize,
                                      rpcRequest->ResponseTimeout,
                                      rpcRequest->FrameTimeout);
        auto response = Modbus::ExtractResponseData(rpcRequest->Function, res.Pdu);

        if (rpcRequest->OnResult) {
            Json::Value replyJSON;
            replyJSON["response"] = FormatResponse(response, rpcRequest->Format);
            rpcRequest->OnResult(replyJSON);
        }

        if (rpcRequest->Function == Modbus::EFunction::FN_WRITE_SINGLE_COIL ||
            rpcRequest->Function == Modbus::EFunction::FN_WRITE_SINGLE_REGISTER ||
            rpcRequest->Function == Modbus::EFunction::FN_WRITE_MULTIPLE_COILS ||
            rpcRequest->Function == Modbus::EFunction::FN_WRITE_MULTIPLE_REGISTERS)
        {
            std::string id = rpcRequest->ParametersCache.GetId(port, std::to_string(rpcRequest->SlaveId));
            rpcRequest->ParametersCache.Remove(id);
        }
    } catch (const Modbus::TModbusExceptionError& error) {
        Json::Value replyJSON;
        replyJSON["exception"]["code"] = error.GetExceptionCode();
        replyJSON["exception"]["msg"] = error.what();
        rpcRequest->OnResult(replyJSON);
    } catch (const TResponseTimeoutException& error) {
        if (rpcRequest->OnError) {
            rpcRequest->OnError(WBMQTT::E_RPC_REQUEST_TIMEOUT, error.what());
        }
    }
}

TRPCPortLoadModbusSerialClientTask::TRPCPortLoadModbusSerialClientTask(const Json::Value& request,
                                                                       WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                                                       WBMQTT::TMqttRpcServer::TErrorCallback onError,
                                                                       TRPCDeviceParametersCache& parametersCache)
    : Request(ParseRPCPortLoadModbusRequest(request, parametersCache))
{
    Request->OnResult = onResult;
    Request->OnError = onError;
    ExpireTime = std::chrono::steady_clock::now() + Request->TotalTimeout;
}

ISerialClientTask::TRunResult TRPCPortLoadModbusSerialClientTask::Run(
    PPort port,
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
        ExecRPCPortLoadModbusRequest(*port, Request);
    } catch (const std::exception& error) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Port IO error: ") + error.what());
        }
    }
    return ISerialClientTask::TRunResult::OK;
}
