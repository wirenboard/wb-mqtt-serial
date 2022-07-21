#include "rpc_handler.h"
#include "rpc_port.h"
#include "rpc_request.h"
#include "serial_client_queue.h"
#include "serial_device.h"
#include "serial_exc.h"
#include "wblib/exceptions.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    std::vector<uint8_t> HexStringToByteVector(const std::string& HexString)
    {
        std::vector<uint8_t> byteVector;
        if (HexString.size() % 2 != 0) {
            throw std::runtime_error("Hex message have odd char count");
        }

        for (unsigned int i = 0; i < HexString.size(); i += 2) {
            auto byte = strtol(HexString.substr(i, 2).c_str(), NULL, 16);
            byteVector.push_back(byte);
        }

        return byteVector;
    }

    std::string ByteVectorToHexString(const std::vector<uint8_t>& ByteVector)
    {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');

        for (size_t i = 0; i < ByteVector.size(); i++) {
            ss << std::hex << std::setw(2) << static_cast<int>(ByteVector[i]);
        }

        return ss.str();
    }

    PRPCRequest ParseRequest(const Json::Value& Request, const Json::Value& RequestSchema)
    {
        PRPCRequest RPCRequest = std::make_shared<TRPCRequest>();

        try {
            WBMQTT::JSON::Validate(Request, RequestSchema);

            std::string messageStr, formatStr;
            WBMQTT::JSON::Get(Request, "response_size", RPCRequest->ResponseSize);
            WBMQTT::JSON::Get(Request, "format", formatStr);
            WBMQTT::JSON::Get(Request, "msg", messageStr);

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

            if (!WBMQTT::JSON::Get(Request, "response_timeout", RPCRequest->ResponseTimeout)) {
                RPCRequest->ResponseTimeout = DefaultResponseTimeout;
            }

            if (!WBMQTT::JSON::Get(Request, "frame_timeout", RPCRequest->FrameTimeout)) {
                RPCRequest->FrameTimeout = DefaultFrameTimeout;
            }

            if (!WBMQTT::JSON::Get(Request, "total_timeout", RPCRequest->TotalTimeout)) {
                RPCRequest->TotalTimeout = DefaultRPCTotalTimeout;
            }

        } catch (const std::runtime_error& e) {
            throw TRPCException(e.what(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
        }

        return RPCRequest;
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
}

namespace RPC_REQUEST
{

    void Handle(PRPCQueueMessage Message)
    {
        // RPC Thread may meet TotalTimeout
        if (Message->Cancelled.load(std::memory_order_relaxed)) {
            Message->Done.store(true, std::memory_order_relaxed);
            Message->Condition.notify_all();
            return;
        }

        try {

            Message->Port->CheckPortOpen();
            Message->Port->SleepSinceLastInteraction(Message->Request->FrameTimeout);
            Message->Port->WriteBytes(Message->Request->Message);

            std::vector<uint8_t> readData;
            readData.reserve(Message->Request->ResponseSize);
            size_t ActualSize = Message->Port->ReadFrame(readData.data(),
                                                         Message->Request->ResponseSize,
                                                         Message->Request->ResponseTimeout,
                                                         Message->Request->FrameTimeout);

            Message->Response.assign(readData.begin(), readData.begin() + ActualSize);

        } catch (TSerialDeviceException error) {
            Message->ResultCode = TRPCRequestResultCode::RPC_REQUEST_WRONG_IO;
            Message->Done.store(true, std::memory_order_relaxed);
            Message->Condition.notify_all();
            return;
        }

        if (Message->Response.size() != Message->Request->ResponseSize) {
            Message->ResultCode = TRPCRequestResultCode::RPC_REQUEST_WRONG_RESPONSE_LENGTH;
        } else {
            Message->ResultCode = TRPCRequestResultCode::RPC_REQUEST_OK;
        }

        Message->Done.store(true, std::memory_order_relaxed);
        Message->Condition.notify_all();
        return;
    }
}

std::vector<uint8_t> TRPCPortDriver::SendRequest(PRPCRequest Request)
{
    std::vector<uint8_t> response;
    PRPCQueueMessage Message = std::make_shared<TRPCQueueMessage>(RPCPort->GetPort(), Request);

    SerialClient->RPCSendQueueMessage(Message);

    auto now = std::chrono::steady_clock::now();
    auto until = now + Request->TotalTimeout;
    std::mutex Mutex;

    std::unique_lock<std::mutex> lock(Mutex);
    if (!Message->Condition.wait_until(lock, until, [Message]() {
            return Message->Done.load(std::memory_order_relaxed);
        })) {
        Message->Cancelled.store(true, std::memory_order_relaxed);
        throw TRPCException("Request handler is not responding", TRPCResultCode::RPC_WRONG_TIMEOUT);
    } else {
        switch (Message->ResultCode) {
            case TRPCRequestResultCode::RPC_REQUEST_WRONG_IO:
                throw TRPCException("Port IO error", TRPCResultCode::RPC_WRONG_IO);

            case TRPCRequestResultCode::RPC_REQUEST_WRONG_RESPONSE_LENGTH:
            case TRPCRequestResultCode::RPC_REQUEST_OK:
                return response = Message->Response;

            default:
                break;
        }
    }

    return response;
}

TRPCHandler::TRPCHandler(const std::string& RequestSchemaFilePath,
                         PRPCConfig RpcConfig,
                         WBMQTT::PMqttRpcServer RpcServer,
                         PMQTTSerialDriver SerialDriver)
{
    try {
        RequestSchema = WBMQTT::JSON::Parse(RequestSchemaFilePath);
    } catch (const std::runtime_error& e) {
        LOG(Error) << "RPC request schema readind error: " << e.what();
        throw std::runtime_error(e);
    }

    for (auto RPCPort: RpcConfig->GetPorts()) {
        PRPCPortDriver RPCPortDriver = std::make_shared<TRPCPortDriver>();
        RPCPortDriver->RPCPort = RPCPort;
        PortDrivers.push_back(RPCPortDriver);
    }

    this->SerialDriver = SerialDriver;

    std::vector<PSerialPortDriver> serialPortDrivers = SerialDriver->GetPortDrivers();
    for (auto serialPortDriver: serialPortDrivers) {
        PPort port = serialPortDriver->GetSerialClient()->GetPort();

        std::find_if(PortDrivers.begin(), PortDrivers.end(), [&serialPortDriver, &port](PRPCPortDriver rpcPortDriver) {
            if (port == rpcPortDriver->RPCPort->GetPort()) {
                rpcPortDriver->SerialClient = serialPortDriver->GetSerialClient();
                return true;
            }
            return false;
        });
    }

    RpcServer->RegisterMethod("port", "Load", std::bind(&TRPCHandler::PortLoad, this, std::placeholders::_1));
    RpcServer->RegisterMethod("metrics", "Load", std::bind(&TRPCHandler::LoadMetrics, this, std::placeholders::_1));
}

PRPCPortDriver TRPCHandler::FindPortDriver(const Json::Value& Request)
{
    std::vector<PRPCPortDriver> matches;
    std::copy_if(PortDrivers.begin(),
                 PortDrivers.end(),
                 std::back_inserter(matches),
                 [&Request](PRPCPortDriver rpcPortDriver) { return rpcPortDriver->RPCPort->Match(Request); });

    switch (matches.size()) {
        case 0:
            throw TRPCException("Requested port doesn't exist", TRPCResultCode::RPC_WRONG_PORT);
        case 1:
            return matches[0];
        default:
            throw TRPCException("More than one matches for requested port", TRPCResultCode::RPC_WRONG_PORT);
    }
}

Json::Value TRPCHandler::PortLoad(const Json::Value& Request)
{
    Json::Value replyJSON;

    try {
        PRPCRequest RPCRequest = ParseRequest(Request, RequestSchema);
        PRPCPortDriver rpcPortDriver = FindPortDriver(Request);
        std::vector<uint8_t> response = rpcPortDriver->SendRequest(RPCRequest);

        std::string responseStr = PortLoadResponseFormat(response, RPCRequest->Format);

        replyJSON["response"] = responseStr;
    } catch (TRPCException& e) {
        LOG(Error) << e.GetResultMessage();
        switch (e.GetResultCode()) {
            case TRPCResultCode::RPC_WRONG_TIMEOUT:
                wb_throw(WBMQTT::TRequestTimeoutException, e.GetResultMessage());
                break;
            default:
                throw std::runtime_error(e.GetResultMessage());
                break;
        }
    }

    return replyJSON;
}

Json::Value TRPCHandler::LoadMetrics(const Json::Value& request)
{
    return SerialDriver->LoadMetrics();
}

TRPCException::TRPCException(const std::string& message, TRPCResultCode resultCode)
    : std::runtime_error(message),
      ResultCode(resultCode)
{}

TRPCResultCode TRPCException::GetResultCode()
{
    return ResultCode;
}

std::string TRPCException::GetResultMessage()
{
    return this->what();
}
