#include "rpc_port_load_handler.h"
#include "rpc_port_handler.h"
#include "rpc_port_load_serial_client_task.h"
#include "serial_port.h"
#include "tcp_port.h"

namespace
{
    std::vector<uint8_t> HexStringToByteVector(const std::string& hexString)
    {
        std::vector<uint8_t> byteVector;
        if (hexString.size() % 2 != 0) {
            throw std::runtime_error("Hex message has odd char count");
        }

        for (size_t i = 0; i < hexString.size(); i += 2) {
            auto byte = strtol(hexString.substr(i, 2).c_str(), NULL, 16);
            byteVector.push_back(byte);
        }

        return byteVector;
    }

    std::string ByteVectorToHexString(const std::vector<uint8_t>& byteVector)
    {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');

        for (size_t i = 0; i < byteVector.size(); i++) {
            ss << std::hex << std::setw(2) << static_cast<int>(byteVector[i]);
        }

        return ss.str();
    }

    std::string PortLoadResponseFormat(const std::vector<uint8_t>& response, TRPCMessageFormat format)
    {
        std::string responseStr;
        if (format == TRPCMessageFormat::RPC_MESSAGE_FORMAT_HEX) {
            responseStr = ByteVectorToHexString(response);
        } else {
            responseStr.assign(response.begin(), response.end());
        }

        return responseStr;
    }

    std::vector<uint8_t> SendRequest(PPort port, PRPCPortLoadRequest rpcRequest)
    {
        port->Open();
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
} // namespace

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

PRPCPortLoadRequest ParseRPCPortLoadRequest(const Json::Value& request, const Json::Value& requestSchema)
{
    PRPCPortLoadRequest RPCRequest = std::make_shared<TRPCPortLoadRequest>();

    try {
        WBMQTT::JSON::Validate(request, requestSchema);

        std::string messageStr, formatStr;
        WBMQTT::JSON::Get(request, "response_size", RPCRequest->ResponseSize);
        WBMQTT::JSON::Get(request, "format", formatStr);
        WBMQTT::JSON::Get(request, "msg", messageStr);

        if (formatStr == "HEX") {
            RPCRequest->Format = TRPCMessageFormat::RPC_MESSAGE_FORMAT_HEX;
        } else {
            RPCRequest->Format = TRPCMessageFormat::RPC_MESSAGE_FORMAT_STR;
        }

        if (RPCRequest->Format == TRPCMessageFormat::RPC_MESSAGE_FORMAT_HEX) {
            RPCRequest->Message = HexStringToByteVector(messageStr);
        } else {
            RPCRequest->Message.assign(messageStr.begin(), messageStr.end());
        }

        if (!WBMQTT::JSON::Get(request, "response_timeout", RPCRequest->ResponseTimeout)) {
            RPCRequest->ResponseTimeout = DefaultResponseTimeout;
        }

        if (!WBMQTT::JSON::Get(request, "frame_timeout", RPCRequest->FrameTimeout)) {
            RPCRequest->FrameTimeout = DefaultFrameTimeout;
        }

        if (!WBMQTT::JSON::Get(request, "total_timeout", RPCRequest->TotalTimeout)) {
            RPCRequest->TotalTimeout = DefaultRPCTotalTimeout;
        }

        RPCRequest->SerialPortSettings = ParseRPCSerialPortSettings(request);

    } catch (const std::runtime_error& e) {
        throw TRPCException(e.what(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }

    return RPCRequest;
}

void RPCPortLoadHandler(PRPCPortLoadRequest rpcRequest,
                        PSerialClient serialClient,
                        WBMQTT::TMqttRpcServer::TResultCallback onResult,
                        WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    rpcRequest->OnResult = [onResult, rpcRequest](const std::vector<uint8_t>& response) {
        Json::Value replyJSON;
        replyJSON["response"] = PortLoadResponseFormat(response, rpcRequest->Format);
        onResult(replyJSON);
    };
    rpcRequest->OnError = onError;
    if (serialClient) {
        auto task(std::make_shared<TRPCPortLoadSerialClientTask>(rpcRequest));
        serialClient->AddTask(task);
    } else {
        throw TRPCException("SerialClient wasn't found for requested port", TRPCResultCode::RPC_WRONG_PORT);
    }
}

void RPCPortLoadHandler(PRPCPortLoadRequest rpcRequest,
                        PPort port,
                        WBMQTT::TMqttRpcServer::TResultCallback onResult,
                        WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    Json::Value replyJSON;
    auto response = SendRequest(port, rpcRequest);
    replyJSON["response"] = PortLoadResponseFormat(response, rpcRequest->Format);
    onResult(replyJSON);
}
