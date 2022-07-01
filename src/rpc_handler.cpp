#include <rpc_handler.h>
#include <rpc_request.h>

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    std::string PortLoadResponseFormat(const std::vector<uint8_t>& response,
                                       size_t actualResponseSize,
                                       std::string format)
    {
        std::string responseStr;

        if (format == "HEX") {
            std::stringstream ss;
            ss << std::hex << std::setfill('0');

            for (size_t i = 0; i < actualResponseSize; i++) {
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

void TRPCHandler::SerialDriverInitialize(PMQTTSerialDriver SerialDriver)
{
    this->SerialDriver = SerialDriver;
    std::vector<PSerialPortDriver> portDrivers = SerialDriver->GetPortDrivers();
    for (auto portDriver: portDrivers) {
        PSerialClient serialClient = portDriver->GetSerialClient();
        PPort port = serialClient->GetPort();
        AddSerialPortDriver(portDriver, port);
    }
}

void TRPCHandler::AddPort(PPort Port, enum TRPCModbusMode Mode, std::string Path, std::string Ip, int PortNumber)
{
    auto existedPort = std::find_if(Ports.begin(), Ports.end(), [&Port](TRPCPort RPCPort) {
        if (Port == RPCPort.Port) {
            return true;
        }
        return false;
    });

    if (existedPort == Ports.end()) {
        TRPCPort newRPCPort;
        newRPCPort.Port = Port;
        newRPCPort.Mode = Mode;
        newRPCPort.Path = Path;
        newRPCPort.Ip = Ip;
        newRPCPort.PortNumber = PortNumber;
        Ports.push_back(newRPCPort);
    }
}

void TRPCHandler::AddSerialPortDriver(PSerialPortDriver SerialPortDriver, PPort Port)
{
    auto existedPort = std::find_if(Ports.begin(), Ports.end(), [&SerialPortDriver, &Port](TRPCPort RPCPort) {
        if (Port == RPCPort.Port) {
            RPCPort.SerialPortDriver = SerialPortDriver;
            return true;
        }
        return false;
    });
}

bool TRPCHandler::FindSerialDriverByPath(enum TRPCModbusMode Mode,
                                         std::string Path,
                                         std::string Ip,
                                         int PortNumber,
                                         PSerialPortDriver SerialPortDriver)
{
    auto existedPort = std::find_if(Ports.begin(), Ports.end(), [&Mode, &Path, &Ip, &PortNumber](TRPCPort RPCPort) {
        if (RPCPort.Mode == TRPCModbusMode::RPC_RTU) {
            if (RPCPort.Path == Path) {
                return true;
            }
        } else {
            if ((RPCPort.Ip == Ip) && (RPCPort.PortNumber == PortNumber)) {
                return true;
            }
        }
        return false;
    });

    if (existedPort != Ports.end()) {
        SerialPortDriver = existedPort->SerialPortDriver;
        return true;
    }

    return false;
}

Json::Value TRPCHandler::PortLoad(const Json::Value& request)
{
    Json::Value replyJSON;
    std::string errorMsg;
    RPCPortHandlerResult resultCode;

    size_t actualResponseSize;
    std::vector<uint8_t> response;
    std::string responseStr;

    PRPCRequest Request;

    try {

        if (!Request->CheckParamSet(request)) {
            throw TRPCException("Wrong mandatory parameters set", RPCPortHandlerResult::RPC_WRONG_PARAM_SET);
        }

        if (!Request->LoadValues(request)) {
            throw TRPCException("Wrong parameters types or values", RPCPortHandlerResult::RPC_WRONG_PARAM_VALUE);
        }

        PSerialPortDriver SerialPortDriver;
        bool find =
            FindSerialDriverByPath(Request->Mode, Request->Path, Request->Ip, Request->PortNumber, SerialPortDriver);
        if (!find) {
            throw TRPCException("Requested port doesn't exist", RPCPortHandlerResult::RPC_WRONG_PORT);
        }

        if (!SerialPortDriver->RPCTransceive(Request, response, actualResponseSize)) {
            throw TRPCException("Port IO error", RPCPortHandlerResult::RPC_WRONG_IO);
        }

        if (actualResponseSize != Request->ResponseSize) {
            throw TRPCException("Actual response length shorter than requested",
                                RPCPortHandlerResult::RPC_WRONG_RESP_LNGTH);
        }

        responseStr = PortLoadResponseFormat(response, actualResponseSize, Request->Format);
        errorMsg = "Success";
        resultCode = RPCPortHandlerResult::RPC_OK;
    } catch (TRPCException& e) {
        LOG(Error) << e.GetResultMessage();
        resultCode = e.GetResultCode();
        errorMsg = e.GetResultMessage();
        responseStr = "";
    }

    replyJSON["result_code"] = std::to_string(static_cast<int>(resultCode));
    replyJSON["error_msg"] = errorMsg;
    replyJSON["response"] = responseStr;

    return replyJSON;
}

Json::Value TRPCHandler::LoadMetrics(const Json::Value& request)
{
    Json::Value metrics;
    SerialDriver->RPCGetMetrics(metrics);
    return metrics;
}