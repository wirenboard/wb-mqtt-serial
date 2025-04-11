#include "rpc_exception.h"

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
