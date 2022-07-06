#include "rpc_handler.h"
#include "rpc_port.h"
#include "rpc_request.h"
#include "serial_device.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    std::tuple<PRPCRequest, TRPCPortMode, std::string, std::string, int> ParseOptions(const Json::Value& Request)
    {

        PRPCRequest RPCRequest = std::make_shared<TRPCRequest>();
        TRPCPortMode Mode;
        std::string Path;
        std::string Ip;
        int PortNumber;

        bool pathEnt = Request.isMember("path");
        bool ipEnt = Request.isMember("ip");
        bool portEnt = Request.isMember("port");
        bool msgEnt = Request.isMember("msg");
        bool respSizeEnt = Request.isMember("response_size");

        bool serialConf = (pathEnt && !ipEnt && !portEnt);
        bool tcpConf = (!pathEnt && ipEnt && portEnt);
        bool paramConf = msgEnt && respSizeEnt;

        bool correct = ((tcpConf || serialConf) && paramConf);
        if (correct) {
            Mode = serialConf ? TRPCPortMode::RPC_RTU : TRPCPortMode::RPC_TCP;
        } else {
            throw TRPCException("Wrong mandatory options set", RPCPortHandlerResult::RPC_WRONG_PARAM_SET);
        }

        if (Mode == TRPCPortMode::RPC_RTU) {
            if (!WBMQTT::JSON::Get(Request, "path", Path)) {
                throw TRPCException("Wrong path option", RPCPortHandlerResult::RPC_WRONG_PARAM_VALUE);
            }
        } else {
            if (!WBMQTT::JSON::Get(Request, "ip", Ip)) {
                throw TRPCException("Wrong ip option", RPCPortHandlerResult::RPC_WRONG_PARAM_VALUE);
            }

            if (!WBMQTT::JSON::Get(Request, "port", PortNumber) || (PortNumber < 0) || (PortNumber > 65536)) {
                throw TRPCException("Wrong port option", RPCPortHandlerResult::RPC_WRONG_PARAM_VALUE);
            }
        }

        if (!WBMQTT::JSON::Get(Request, "response_size", RPCRequest->ResponseSize) || (RPCRequest->ResponseSize < 0)) {
            throw TRPCException("Missing response_size option", RPCPortHandlerResult::RPC_WRONG_PARAM_VALUE);
        }

        if (!WBMQTT::JSON::Get(Request, "response_timeout", RPCRequest->ResponseTimeout)) {
            RPCRequest->ResponseTimeout = DefaultResponseTimeout;
        }

        if (!WBMQTT::JSON::Get(Request, "frame_timeout", RPCRequest->FrameTimeout)) {
            RPCRequest->FrameTimeout = DefaultFrameTimeout;
        }

        RPCRequest->TotalTimeout = DefaultDeviceTimeout;

        if (!WBMQTT::JSON::Get(Request, "format", RPCRequest->Format)) {
            RPCRequest->Format = "";
        }

        std::string messageStr;
        if (!WBMQTT::JSON::Get(Request, "msg", messageStr) || (messageStr == "")) {
            throw TRPCException("Missing msg option", RPCPortHandlerResult::RPC_WRONG_PARAM_VALUE);
        }

        RPCRequest->Message.clear();
        if (RPCRequest->Format == "HEX") {
            if (messageStr.size() % 2 != 0) {
                throw std::exception();
            }

            for (unsigned int i = 0; i < messageStr.size(); i += 2) {
                auto byte = strtol(messageStr.substr(i, 2).c_str(), NULL, 16);
                RPCRequest->Message.push_back(byte);
            }
        } else {
            RPCRequest->Message.assign(messageStr.begin(), messageStr.end());
        }

        return std::make_tuple(RPCRequest, Mode, Path, Ip, PortNumber);
    }

    std::string PortLoadResponseFormat(const std::vector<uint8_t>& response, std::string format)
    {
        std::string responseStr;

        if (format == "HEX") {
            std::stringstream ss;
            ss << std::hex << std::setfill('0');

            for (size_t i = 0; i < response.size(); i++) {
                ss << std::hex << std::setw(2) << static_cast<int>(response[i]);
            }

            responseStr = ss.str();
        } else {
            responseStr.assign(response.begin(), response.end());
        }

        return responseStr;
    }
}

void TRPCHandler::RPCServerInitialize(WBMQTT::PMqttRpcServer RpcServer)
{
    RpcServer->RegisterMethod("port", "Load", std::bind(&TRPCHandler::PortLoad, this, std::placeholders::_1));
    RpcServer->RegisterMethod("metrics", "Load", std::bind(&TRPCHandler::LoadMetrics, this, std::placeholders::_1));
}

void TRPCHandler::RPCConfigInitialize(PRPCConfig RpcConfig)
{
    this->Config = RpcConfig;

    for (auto RPCPort: Config->GetPorts()) {
        PRPCPortDriver RPCPortDriver = std::make_shared<TRPCPortDriver>();
        RPCPortDriver->Mode = RPCPort->Mode;
        RPCPortDriver->Path = RPCPort->Path;
        RPCPortDriver->Ip = RPCPort->Ip;
        RPCPortDriver->Port = RPCPort->Port;
        RPCPortDriver->PortNumber = RPCPortDriver->PortNumber;
        PortDrivers.push_back(RPCPortDriver);
    }
}

void TRPCHandler::SerialDriverInitialize(PMQTTSerialDriver SerialDriver)
{
    this->SerialDriver = SerialDriver;
    std::vector<PSerialPortDriver> serialPortDrivers = SerialDriver->GetPortDrivers();
    for (auto serialPortDriver: serialPortDrivers) {
        PSerialClient serialClient = serialPortDriver->GetSerialClient();
        PPort port = serialClient->GetPort();

        std::find_if(PortDrivers.begin(), PortDrivers.end(), [&serialPortDriver, &port](PRPCPortDriver rpcPortDriver) {
            if (port == rpcPortDriver->Port) {
                rpcPortDriver->SerialPortDriver = serialPortDriver;
                return true;
            }
            return false;
        });
    }
}

PSerialPortDriver TRPCHandler::FindSerialPortDriverByPath(enum TRPCPortMode Mode,
                                                          std::string Path,
                                                          std::string Ip,
                                                          int PortNumber)
{
    auto existedPortDriver =
        std::find_if(PortDrivers.begin(),
                     PortDrivers.end(),
                     [&Mode, &Path, &Ip, &PortNumber](PRPCPortDriver rpcPortDriver) {
                         if (rpcPortDriver->Mode == TRPCPortMode::RPC_RTU) {
                             if (rpcPortDriver->Path == Path) {
                                 return true;
                             }
                         } else {
                             if ((rpcPortDriver->Ip == Ip) && (rpcPortDriver->PortNumber == PortNumber)) {
                                 return true;
                             }
                         }
                         return false;
                     });

    if (existedPortDriver != PortDrivers.end()) {
        return existedPortDriver->get()->SerialPortDriver;
    }

    throw TRPCException("Requested port doesn't exist", RPCPortHandlerResult::RPC_WRONG_PORT);
}

Json::Value TRPCHandler::PortLoad(const Json::Value& Request)
{
    Json::Value replyJSON;
    std::string errorMsg;
    RPCPortHandlerResult resultCode;

    std::vector<uint8_t> response;
    std::string responseStr;

    PRPCRequest RPCRequest;
    TRPCPortMode Mode;
    int PortNumber;
    std::string Path, Ip;

    try {

        std::tie(RPCRequest, Mode, Path, Ip, PortNumber) = ParseOptions(Request);
        PSerialPortDriver SerialPortDriver = FindSerialPortDriverByPath(Mode, Path, Ip, PortNumber);
        response = SerialPortDriver->RPCTransceive(RPCRequest);

        responseStr = PortLoadResponseFormat(response, RPCRequest->Format);
        errorMsg = "Success";
        resultCode = RPCPortHandlerResult::RPC_OK;
    } catch (TRPCException& e) {
        LOG(Error) << e.GetResultMessage();
        resultCode = e.GetResultCode();
        errorMsg = e.GetResultMessage();
        responseStr = PortLoadResponseFormat(response, RPCRequest->Format);
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