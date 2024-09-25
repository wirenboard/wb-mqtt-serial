#include "rpc_port_load_modbus_serial_client_task.h"
#include "modbus_base.h"
#include "rpc_port_handler.h"
#include "rpc_port_load_handler.h"
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

PRPCPortLoadModbusRequest ParseRPCPortLoadModbusRequest(const Json::Value& request)
{
    PRPCPortLoadModbusRequest RPCRequest = std::make_shared<TRPCPortLoadModbusRequest>();

    try {
        ParseRPCPortLoadRequest(request, *RPCRequest);
        WBMQTT::JSON::Get(request, "slave_id", RPCRequest->SlaveId);
        WBMQTT::JSON::Get(request, "address", RPCRequest->Address);
        WBMQTT::JSON::Get(request, "count", RPCRequest->Count);
        WBMQTT::JSON::Get(request, "function", RPCRequest->Function);
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
        Modbus::TModbusRTUTraits traits;
        auto pdu = Modbus::MakePDU(rpcRequest->Function, rpcRequest->Address, rpcRequest->Count, rpcRequest->Message);
        auto responsePduSize = Modbus::CalcResponsePDUSize(rpcRequest->Function, rpcRequest->Count);
        auto res = traits.Transaction(port,
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

TRPCPortLoadModbusSerialClientTask::TRPCPortLoadModbusSerialClientTask(PRPCPortLoadModbusRequest request)
    : Request(request)
{
    Request = request;
    ExpireTime = std::chrono::steady_clock::now() + Request->TotalTimeout;
}

ISerialClientTask::TRunResult TRPCPortLoadModbusSerialClientTask::Run(
    PPort port,
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
        ExecRPCPortLoadModbusRequest(*port, Request);
    } catch (const std::exception& error) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Port IO error: ") + error.what());
        }
    }
    return ISerialClientTask::TRunResult::OK;
}
