#include "rpc_helpers.h"
#include "rpc_exception.h"
#include "serial_port.h"
#include "tcp_port.h"

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
