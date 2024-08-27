#include "rpc_port_load_raw_serial_client_task.h"
#include "rpc_port_handler.h"
#include "rpc_port_load_handler.h"
#include "serial_exc.h"
#include "serial_port.h"

namespace
{
    std::vector<uint8_t> SendRequest(PPort port, PRPCPortLoadRawRequest rpcRequest)
    {
        port->WriteBytes(rpcRequest->Message);

        std::vector<uint8_t> response(rpcRequest->ResponseSize);
        auto actualSize = port->ReadFrame(response.data(),
                                          rpcRequest->ResponseSize,
                                          rpcRequest->ResponseTimeout,
                                          rpcRequest->FrameTimeout)
                              .Count;
        response.resize(actualSize);

        return response;
    }
}

PRPCPortLoadRawRequest ParseRPCPortLoadRawRequest(const Json::Value& request)
{
    PRPCPortLoadRawRequest RPCRequest = std::make_shared<TRPCPortLoadRawRequest>();

    try {
        ParseRPCPortLoadRequest(request, *RPCRequest);
        WBMQTT::JSON::Get(request, "response_size", RPCRequest->ResponseSize);
    } catch (const std::runtime_error& e) {
        throw TRPCException(e.what(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }

    return RPCRequest;
}

std::vector<uint8_t> ExecRPCPortLoadRawRequest(PPort port, PRPCPortLoadRawRequest rpcRequest)
{
    port->Open();
    return SendRequest(port, rpcRequest);
}

TRPCPortLoadRawSerialClientTask::TRPCPortLoadRawSerialClientTask(PRPCPortLoadRawRequest request): Request(request)
{
    Request = request;
    ExpireTime = std::chrono::steady_clock::now() + Request->TotalTimeout;
}

ISerialClientTask::TRunResult TRPCPortLoadRawSerialClientTask::Run(PPort port,
                                                                   TSerialClientDeviceAccessHandler& lastAccessedDevice)
{
    if (std::chrono::steady_clock::now() > ExpireTime) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_REQUEST_TIMEOUT, "RPC request timeout");
        }
        return ISerialClientTask::TRunResult::OK;
    }

    try {
        port->CheckPortOpen();
        port->SkipNoise();
        port->SleepSinceLastInteraction(Request->FrameTimeout);
        lastAccessedDevice.PrepareToAccess(nullptr);

        TSerialPortSettingsGuard settingsGuard(port, Request->SerialPortSettings);
        auto response = SendRequest(port, Request);

        if (Request->OnResult) {
            Json::Value replyJSON;
            replyJSON["response"] = FormatResponse(response, Request->Format);
            Request->OnResult(replyJSON);
        }
    } catch (const std::exception& error) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Port IO error: ") + error.what());
        }
    }
    return ISerialClientTask::TRunResult::OK;
}
