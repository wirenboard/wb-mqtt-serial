#include "rpc_exception.h"
#include "log.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

TRPCException::TRPCException(const std::string& message, TRPCResultCode resultCode)
    : std::runtime_error(message),
      ResultCode(resultCode)
{}

TRPCResultCode TRPCException::GetResultCode() const
{
    return ResultCode;
}

std::string TRPCException::GetResultMessage() const
{
    return this->what();
}

void ProcessException(const TRPCException& e, WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    if (e.GetResultCode() == TRPCResultCode::RPC_WRONG_IO) {
        // Too many "request timed out" errors while scanning ports
        LOG(Debug) << e.GetResultMessage();
    } else {
        LOG(Warn) << e.GetResultMessage();
    }
    switch (e.GetResultCode()) {
        case TRPCResultCode::RPC_WRONG_TIMEOUT: {
            onError(WBMQTT::E_RPC_REQUEST_TIMEOUT, e.GetResultMessage());
            break;
        }
        default: {
            onError(WBMQTT::E_RPC_SERVER_ERROR, e.GetResultMessage());
            break;
        }
    }
}
