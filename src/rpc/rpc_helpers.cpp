#include "rpc_helpers.h"
#include "log.h"
#include "rpc_exception.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

TSerialPortConnectionSettings ParseRPCSerialPortSettings(const Json::Value& request)
{
    TSerialPortConnectionSettings res;
    WBMQTT::JSON::Get(request, "baud_rate", res.BaudRate);
    if (request.isMember("parity")) {
        res.Parity = request["parity"].asCString()[0];
    }
    WBMQTT::JSON::Get(request, "data_bits", res.DataBits);
    WBMQTT::JSON::Get(request, "stop_bits", res.StopBits);
    return res;
}

void ValidateRPCRequest(const Json::Value& request, const Json::Value& schema)
{
    try {
        WBMQTT::JSON::Validate(request, schema);
    } catch (const std::runtime_error& e) {
        throw TRPCException(e.what(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
}

Json::Value LoadRPCRequestSchema(const std::string& schemaFilePath, const std::string& rpcName)
{
    try {
        return WBMQTT::JSON::Parse(schemaFilePath);
    } catch (const std::runtime_error& e) {
        LOG(Error) << "RPC " + rpcName + " request schema reading error: " << e.what();
        throw;
    }
}
