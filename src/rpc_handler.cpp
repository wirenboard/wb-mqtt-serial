#include "rpc_handler.h"
#include "queue.h"
#include "rpc_port.h"
#include "rpc_request.h"
#include "serial_device.h"
#include "serial_exc.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

const auto RPC_REQUEST_SCHEMA_FULL_FILE_PATH = "/usr/share/wb-mqtt-serial/wb-mqtt-serial-rpc-request.schema.json";

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

    PRPCOptions ParseOptions(const Json::Value& Request, const Json::Value& RequestSchema)
    {
        PRPCOptions options = std::make_shared<TRPCOptions>();
        options->RPCRequest = std::make_shared<TRPCRequest>();

        try {
            WBMQTT::JSON::Validate(Request, RequestSchema);

            std::string Path;
            if (WBMQTT::JSON::Get(Request, "path", Path)) {
                options->RPCPort = std::make_shared<TRPCSerialPort>(nullptr, Path);
            } else {
                std::string Ip;
                int PortNumber;
                WBMQTT::JSON::Get(Request, "ip", Ip);
                WBMQTT::JSON::Get(Request, "port", PortNumber);
                options->RPCPort = std::make_shared<TRPCTCPPort>(nullptr, Ip, (uint16_t)PortNumber);
            }

            std::string messageStr, formatStr;
            WBMQTT::JSON::Get(Request, "response_size", options->RPCRequest->ResponseSize);
            WBMQTT::JSON::Get(Request, "format", formatStr);
            WBMQTT::JSON::Get(Request, "msg", messageStr);

            if (formatStr == "HEX") {
                options->RPCRequest->Format = TRPCMessageFormat::RPC_MESSAGE_FORMAT_HEX;
            } else {
                options->RPCRequest->Format = TRPCMessageFormat::RPC_MESSAGE_FORMAT_STR;
            }

            if (options->RPCRequest->Format == TRPCMessageFormat::RPC_MESSAGE_FORMAT_HEX) {
                options->RPCRequest->Message = HexStringToByteVector(messageStr);
            } else {
                options->RPCRequest->Message.assign(messageStr.begin(), messageStr.end());
            }

            if (!WBMQTT::JSON::Get(Request, "response_timeout", options->RPCRequest->ResponseTimeout)) {
                options->RPCRequest->ResponseTimeout = DefaultResponseTimeout;
            }

            if (!WBMQTT::JSON::Get(Request, "frame_timeout", options->RPCRequest->FrameTimeout)) {
                options->RPCRequest->FrameTimeout = DefaultFrameTimeout;
            }

            if (!WBMQTT::JSON::Get(Request, "total_timeout", options->RPCRequest->FrameTimeout)) {
                options->RPCRequest->TotalTimeout = DefaultRPCTotalTimeout;
            }

        } catch (const std::runtime_error& e) {
            throw TRPCException(e.what(), RPCResultCode::RPC_WRONG_PARAM_VALUE);
        }

        return options;
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
        if (Message->Cancelled->load(std::memory_order_relaxed)) {
            Message->Done->store(true, std::memory_order_relaxed);
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

            Message->Response->assign(readData.begin(), readData.begin() + ActualSize);

        } catch (TSerialDeviceException error) {
            *Message->ResultCode = TRPCRequestResultCode::RPC_REQUEST_WRONG_IO;
            Message->Done->store(true, std::memory_order_relaxed);
            Message->Condition.notify_all();
            return;
        }

        if (Message->Response->size() != Message->Request->ResponseSize) {
            *Message->ResultCode = TRPCRequestResultCode::RPC_REQUEST_WRONG_RESPONSE_LENGTH;
        } else {
            *Message->ResultCode = TRPCRequestResultCode::RPC_REQUEST_OK;
        }

        Message->Done->store(true, std::memory_order_relaxed);
        Message->Condition.notify_all();
        return;
    }
}

std::vector<uint8_t> TRPCPortDriver::SendRequest(PRPCRequest Request)
{
    std::vector<uint8_t> response;
    PRPCQueueMessage Message = std::make_shared<TRPCQueueMessage>();
    Message->Port = RPCPort->GetPort();
    Message->Request = Request;

    Message->Cancelled = std::make_shared<std::atomic<bool>>(false);
    Message->Done = std::make_shared<std::atomic<bool>>(false);
    Message->Response = std::make_shared<std::vector<uint8_t>>();
    Message->ResultCode = std::make_shared<TRPCRequestResultCode>();

    SerialPortDriver->RPCSendQueueMessage(Message);

    auto now = std::chrono::steady_clock::now();
    auto until = now + Request->TotalTimeout;
    std::mutex Mutex;

    std::unique_lock<std::mutex> lock(Mutex);
    if (!Message->Condition.wait_until(lock, until, [Message]() {
            return Message->Done->load(std::memory_order_relaxed);
        })) {
        Message->Cancelled->store(true, std::memory_order_relaxed);
        throw TRPCException("Request handler is not responding", RPCResultCode::RPC_WRONG_IO);
    } else {
        switch (*Message->ResultCode) {
            case TRPCRequestResultCode::RPC_REQUEST_WRONG_IO:
                throw TRPCException("Port IO error", RPCResultCode::RPC_WRONG_IO);

            case TRPCRequestResultCode::RPC_REQUEST_WRONG_RESPONSE_LENGTH:
                throw TRPCException("Actual response length shorter than requested",
                                    RPCResultCode::RPC_WRONG_RESPONSE_LENGTH);

            case TRPCRequestResultCode::RPC_REQUEST_OK:
                return response = *Message->Response;

            default:
                break;
        }
    }

    return response;
}

TRPCHandler::TRPCHandler(PRPCConfig RpcConfig, WBMQTT::PMqttRpcServer RpcServer, PMQTTSerialDriver SerialDriver)
{
    try {
        RequestSchema = WBMQTT::JSON::Parse(RPC_REQUEST_SCHEMA_FULL_FILE_PATH);
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
                rpcPortDriver->SerialPortDriver = serialPortDriver;
                return true;
            }
            return false;
        });
    }

    RpcServer->RegisterMethod("port", "Load", std::bind(&TRPCHandler::PortLoad, this, std::placeholders::_1));
    RpcServer->RegisterMethod("metrics", "Load", std::bind(&TRPCHandler::LoadMetrics, this, std::placeholders::_1));
}

PRPCPortDriver TRPCHandler::FindPortDriver(PRPCPort RequestedPort)
{
    auto existedPortDriver =
        std::find_if(PortDrivers.begin(), PortDrivers.end(), [&RequestedPort](PRPCPortDriver rpcPortDriver) {
            if (rpcPortDriver->RPCPort->Compare(RequestedPort)) {
                return true;
            }

            return false;
        });

    if (existedPortDriver != PortDrivers.end()) {
        return *existedPortDriver;
    }

    throw TRPCException("Requested port doesn't exist", RPCResultCode::RPC_WRONG_PORT);
}

Json::Value TRPCHandler::PortLoad(const Json::Value& Request)
{
    Json::Value replyJSON;
    std::string errorMsg;
    RPCResultCode resultCode;

    std::string responseStr;

    try {
        PRPCOptions options = ParseOptions(Request, RequestSchema);
        PRPCPortDriver rpcPortDriver = FindPortDriver(options->RPCPort);
        std::vector<uint8_t> response = rpcPortDriver->SendRequest(options->RPCRequest);

        responseStr = PortLoadResponseFormat(response, options->RPCRequest->Format);
        errorMsg = "Success";
        resultCode = RPCResultCode::RPC_OK;
    } catch (TRPCException& e) {
        LOG(Error) << e.GetResultMessage();
        resultCode = e.GetResultCode();
        errorMsg = e.GetResultMessage();
        // responseStr = PortLoadResponseFormat(response, options.RPCRequest->Format);
    }

    replyJSON["result_code"] = std::to_string(static_cast<int>(resultCode));
    replyJSON["error_msg"] = errorMsg;
    replyJSON["response"] = responseStr;

    return replyJSON;
}

Json::Value TRPCHandler::LoadMetrics(const Json::Value& request)
{
    return SerialDriver->LoadMetrics();
}

TRPCException::TRPCException(const std::string& message, RPCResultCode resultCode)
    : std::runtime_error(message),
      Message(message),
      ResultCode(resultCode)
{}

RPCResultCode TRPCException::GetResultCode()
{
    return ResultCode;
}

std::string TRPCException::GetResultMessage()
{
    return Message;
}
