#include "rpc_port_handler.h"
#include "rpc_port.h"
#include "rpc_port_load_handler.h"
#include "rpc_port_setup_handler.h"
#include "serial_device.h"
#include "serial_exc.h"
#include "serial_port.h"
#include "tcp_port.h"
#include "wblib/exceptions.h"

#define LOG(logger) ::logger.Log() << "[RPC] "

namespace
{
    PPort InitPort(const Json::Value& request)
    {
        if (request.isMember("path")) {
            std::string path;
            WBMQTT::JSON::Get(request, "path", path);
            TSerialPortSettings settings(path, ParseRPCSerialPortSettings(request));

            LOG(Debug) << "Create serial port: " << path;
            return std::make_shared<TSerialPort>(settings);
        }
        if (request.isMember("ip") && request.isMember("port")) {
            std::string address;
            int portNumber = 0;
            WBMQTT::JSON::Get(request, "ip", address);
            WBMQTT::JSON::Get(request, "port", portNumber);
            TTcpPortSettings settings(address, portNumber);

            LOG(Debug) << "Create tcp port: " << address << ":" << portNumber;
            return std::make_shared<TTcpPort>(settings);
        }
        throw TRPCException("Port is not defined", TRPCResultCode::RPC_WRONG_PARAM_VALUE);
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

} // namespace

TRPCPortHandler::TRPCPortHandler(const std::string& requestPortLoadSchemaFilePath,
                                 const std::string& requestPortSetupSchemaFilePath,
                                 PRPCConfig rpcConfig,
                                 WBMQTT::PMqttRpcServer rpcServer,
                                 PMQTTSerialDriver serialDriver)
    : RPCConfig(rpcConfig)
{
    try {
        RequestPortLoadSchema = WBMQTT::JSON::Parse(requestPortLoadSchemaFilePath);
    } catch (const std::runtime_error& e) {
        LOG(Error) << "RPC port/Load request schema reading error: " << e.what();
        throw;
    }

    try {
        RequestPortSetupSchema = WBMQTT::JSON::Parse(requestPortSetupSchemaFilePath);
    } catch (const std::runtime_error& e) {
        LOG(Error) << "RPC port/Setup request schema reading error: " << e.what();
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

        auto findedPortDriver =
            std::find_if(PortDrivers.begin(), PortDrivers.end(), [&port](PRPCPortDriver rpcPortDriver) {
                return port == rpcPortDriver->RPCPort->GetPort();
            });

        if (findedPortDriver != PortDrivers.end()) {
            findedPortDriver->get()->SerialClient = serialPortDriver->GetSerialClient();
        } else {
            LOG(Warn) << "Can't find RPCPortDriver for " << port->GetDescription() << " port";
        }
    }

    rpcServer->RegisterAsyncMethod("port",
                                   "Load",
                                   std::bind(&TRPCPortHandler::PortLoad,
                                             this,
                                             std::placeholders::_1,
                                             std::placeholders::_2,
                                             std::placeholders::_3));
    rpcServer->RegisterMethod("ports", "Load", std::bind(&TRPCPortHandler::LoadPorts, this, std::placeholders::_1));
    rpcServer->RegisterAsyncMethod("port",
                                   "Setup",
                                   std::bind(&TRPCPortHandler::PortSetup,
                                             this,
                                             std::placeholders::_1,
                                             std::placeholders::_2,
                                             std::placeholders::_3));
}

PRPCPortDriver TRPCPortHandler::FindPortDriver(const Json::Value& request) const
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

void TRPCPortHandler::PortLoad(const Json::Value& request,
                               WBMQTT::TMqttRpcServer::TResultCallback onResult,
                               WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    try {
        PRPCPortLoadRequest rpcRequest = ParseRPCPortLoadRequest(request, RequestPortLoadSchema);
        PRPCPortDriver rpcPortDriver = FindPortDriver(request);

        if (rpcPortDriver != nullptr && rpcPortDriver->SerialClient) {
            RPCPortLoadHandler(rpcRequest, rpcPortDriver->SerialClient, onResult, onError);
        } else {
            RPCPortLoadHandler(rpcRequest, InitPort(request), onResult, onError);
        }
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }
}

void TRPCPortHandler::PortSetup(const Json::Value& request,
                                WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    try {
        PRPCPortSetupRequest rpcRequest = ParseRPCPortSetupRequest(request, RequestPortSetupSchema);
        PRPCPortDriver rpcPortDriver = FindPortDriver(request);

        if (rpcPortDriver != nullptr && rpcPortDriver->SerialClient) {
            RPCPortSetupHandler(rpcRequest, rpcPortDriver->SerialClient, onResult, onError);
        } else {
            RPCPortSetupHandler(rpcRequest, InitPort(request), onResult, onError);
        }
    } catch (const TRPCException& e) {
        ProcessException(e, onError);
    }
}

Json::Value TRPCPortHandler::LoadPorts(const Json::Value& request)
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
