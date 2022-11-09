#include "rpc_handler.h"
#include "rpc_port.h"
#include "rpc_request.h"
#include "serial_device.h"
#include "serial_exc.h"
#include "serial_port.h"
#include "tcp_port.h"
#include "wblib/exceptions.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

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

    PRPCRequest ParseRequest(const Json::Value& request, const Json::Value& requestSchema)
    {
        PRPCRequest RPCRequest = std::make_shared<TRPCRequest>();

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

            if (request.isMember("path")) {
                WBMQTT::JSON::Get(request, "baud_rate", RPCRequest->SerialPortSettings.BaudRate);
                RPCRequest->SerialPortSettings.Parity = request["parity"].asCString()[0];
                WBMQTT::JSON::Get(request, "data_bits", RPCRequest->SerialPortSettings.DataBits);
                WBMQTT::JSON::Get(request, "stop_bits", RPCRequest->SerialPortSettings.StopBits);
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

    std::vector<uint8_t> SendRequest(const Json::Value& request, PRPCRequest rpcRequest)
    {
        PPort port;
        if (request.isMember("path")) {
            std::string path;
            WBMQTT::JSON::Get(request, "path", path);
            TSerialPortSettings settings(path, rpcRequest->SerialPortSettings);

            LOG(Debug) << "Create serial port: " << path;
            port = std::make_shared<TSerialPort>(settings);

        } else if (request.isMember("ip") && request.isMember("port")) {
            std::string address;
            int portNumber;
            WBMQTT::JSON::Get(request, "ip", address);
            WBMQTT::JSON::Get(request, "port", portNumber);
            TTcpPortSettings settings(address, portNumber);

            LOG(Debug) << "Create tcp port: " << address << ":" << portNumber;
            port = std::make_shared<TTcpPort>(settings);
        }

        port->Open();
        port->WriteBytes(rpcRequest->Message);

        std::vector<uint8_t> response(rpcRequest->ResponseSize);
        port->ReadFrame(response.data(),
                        rpcRequest->ResponseSize,
                        rpcRequest->ResponseTimeout,
                        rpcRequest->FrameTimeout);

        return response;
    }
} // namespace

std::vector<uint8_t> TRPCPortDriver::SendRequest(PRPCRequest request) const
{
    if (SerialClient) {
        return SerialClient->RPCTransceive(request);
    } else {
        throw TRPCException("SerialClient wasn't found for requested port", TRPCResultCode::RPC_WRONG_PORT);
    }
}

TRPCHandler::TRPCHandler(const std::string& requestSchemaFilePath,
                         PRPCConfig rpcConfig,
                         WBMQTT::PMqttRpcServer rpcServer,
                         PMQTTSerialDriver serialDriver)
    : RPCConfig(rpcConfig)
{
    try {
        RequestSchema = WBMQTT::JSON::Parse(requestSchemaFilePath);
    } catch (const std::runtime_error& e) {
        LOG(Error) << "RPC request schema reading error: " << e.what();
        throw;
    }

    for (auto RPCPort: RPCConfig->GetPorts()) {
        PRPCPortDriver RPCPortDriver = std::make_shared<TRPCPortDriver>();
        RPCPortDriver->RPCPort = RPCPort;
        PortDrivers.push_back(RPCPortDriver);
    }

    this->SerialDriver = serialDriver;

    std::vector<PSerialPortDriver> serialPortDrivers = SerialDriver->GetPortDrivers();
    for (auto serialPortDriver: serialPortDrivers) {
        PPort port = serialPortDriver->GetSerialClient()->GetPort();

        auto findedPortDriver = std::find_if(PortDrivers.begin(),
                                             PortDrivers.end(),
                                             [&serialPortDriver, &port](PRPCPortDriver rpcPortDriver) {
                                                 return port == rpcPortDriver->RPCPort->GetPort();
                                             });

        if (findedPortDriver != PortDrivers.end()) {
            findedPortDriver->get()->SerialClient = serialPortDriver->GetSerialClient();
        } else {
            LOG(Warn) << "Can't find RPCPortDriver for " << port->GetDescription() << " port";
        }
    }

    rpcServer->RegisterMethod("port", "Load", std::bind(&TRPCHandler::PortLoad, this, std::placeholders::_1));
    rpcServer->RegisterMethod("metrics", "Load", std::bind(&TRPCHandler::LoadMetrics, this, std::placeholders::_1));
    rpcServer->RegisterMethod("ports", "Load", std::bind(&TRPCHandler::LoadPorts, this, std::placeholders::_1));
}

PRPCPortDriver TRPCHandler::FindPortDriver(const Json::Value& request) const
{
    std::vector<PRPCPortDriver> matches;
    std::copy_if(PortDrivers.begin(),
                 PortDrivers.end(),
                 std::back_inserter(matches),
                 [&request](PRPCPortDriver rpcPortDriver) { return rpcPortDriver->RPCPort->Match(request); });

    switch (matches.size()) {
        case 0:
            return nullptr;
        case 1:
            return matches[0];
        default:
            throw TRPCException("More than one matches for requested port", TRPCResultCode::RPC_WRONG_PORT);
    }
}

Json::Value TRPCHandler::PortLoad(const Json::Value& request)
{
    Json::Value replyJSON;

    try {
        PRPCRequest rpcRequest = ParseRequest(request, RequestSchema);
        PRPCPortDriver rpcPortDriver = FindPortDriver(request);

        std::vector<uint8_t> response;
        if (rpcPortDriver != nullptr && rpcPortDriver->SerialClient) {
            response = rpcPortDriver->SendRequest(rpcRequest);
        } else {
            response = SendRequest(request, rpcRequest);
        }

        std::string responseStr = PortLoadResponseFormat(response, rpcRequest->Format);

        replyJSON["response"] = responseStr;
    } catch (TRPCException& e) {
        LOG(Warn) << e.GetResultMessage();
        switch (e.GetResultCode()) {
            case TRPCResultCode::RPC_WRONG_TIMEOUT:
                wb_throw(WBMQTT::TRequestTimeoutException, e.GetResultMessage());
                break;
            default:
                throw std::runtime_error(e.GetResultMessage());
        }
    }

    return replyJSON;
}

Json::Value TRPCHandler::LoadMetrics(const Json::Value& request)
{
    return SerialDriver->LoadMetrics();
}

Json::Value TRPCHandler::LoadPorts(const Json::Value& request)
{
    return RPCConfig->GetPortConfigs();
}

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
