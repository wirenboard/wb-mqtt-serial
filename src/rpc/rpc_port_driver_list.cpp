#include "rpc_port_driver_list.h"
#include "serial_port.h"
#include "tcp_port.h"

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

TRPCPortDriverList::TRPCPortDriverList(PRPCConfig rpcConfig, PMQTTSerialDriver serialDriver)
{
    for (auto RPCPort: rpcConfig->GetPorts()) {
        PRPCPortDriver RPCPortDriver = std::make_shared<TRPCPortDriver>();
        RPCPortDriver->RPCPort = RPCPort;
        Drivers.push_back(RPCPortDriver);
    }

    std::vector<PSerialPortDriver> serialPortDrivers = serialDriver->GetPortDrivers();

    for (auto serialPortDriver: serialPortDrivers) {
        PPort port = serialPortDriver->GetSerialClient()->GetPort();

        auto driver = std::find_if(Drivers.begin(), Drivers.end(), [&port](PRPCPortDriver rpcPortDriver) {
            return port == rpcPortDriver->RPCPort->GetPort();
        });

        if (driver != Drivers.end()) {
            driver->get()->SerialClient = serialPortDriver->GetSerialClient();
        } else {
            LOG(Warn) << "Can't find RPCPortDriver for " << port->GetDescription() << " port";
        }
    }
}

PRPCPortDriver TRPCPortDriverList::Find(const Json::Value& request) const
{
    std::vector<PRPCPortDriver> matches;
    std::copy_if(Drivers.begin(), Drivers.end(), std::back_inserter(matches), [&request](PRPCPortDriver rpcPortDriver) {
        return rpcPortDriver->RPCPort->Match(request);
    });

    switch (matches.size()) {
        case 0:
            return nullptr;
        case 1:
            return matches[0];
        default:
            throw TRPCException("More than one matches for requested port", TRPCResultCode::RPC_WRONG_PORT);
    }
}

PPort TRPCPortDriverList::InitPort(const Json::Value& request)
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

void TRPCPortDriverList::ProcessException(const TRPCException& e, WBMQTT::TMqttRpcServer::TErrorCallback onError)
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
