#include "rpc_port_settings.h"
#include "rpc_exception.h"
#include "rpc_helpers.h"

std::string GetRPCPortDescription(const TRPCPortSettings& portSettings)
{
    if (const auto* tcpPort = std::get_if<TRPCTcpPortSettings>(&portSettings)) {
        return tcpPort->GetDescription();
    }
    return std::get<TSerialPortSettings>(portSettings).Device;
}

TSerialPortConnectionSettings GetRPCPortConnectionSettings(const TRPCPortSettings& portSettings)
{
    if (const auto* serialPort = std::get_if<TSerialPortSettings>(&portSettings)) {
        return *serialPort;
    }
    return TSerialPortConnectionSettings();
}

TRPCPortSettings ParseRPCPort(const Json::Value& json,
                              bool modbusTcp,
                              const TSerialPortConnectionSettings& defaultSerialSettings)
{
    std::string path;
    if (WBMQTT::JSON::Get(json, "path", path) && !path.empty()) {
        return TSerialPortSettings(path, ParseRPCSerialPortSettings(json, defaultSerialSettings));
    }

    // "ip" is the field name of device/* and port/* RPCs, "address" of fw-update.
    // port/Load has an integer "address" of its own, the Modbus register to read
    std::string address;
    int port = 0;
    if ((json["ip"].isString() || json["address"].isString()) &&
        (WBMQTT::JSON::Get(json, "ip", address) || WBMQTT::JSON::Get(json, "address", address)) && !address.empty() &&
        WBMQTT::JSON::Get(json, "port", port))
    {
        return TRPCTcpPortSettings{TTcpPortSettings(address, static_cast<uint16_t>(port)), modbusTcp};
    }

    throw TRPCException("Port is not defined", TRPCResultCode::RPC_WRONG_PARAM_VALUE);
}
