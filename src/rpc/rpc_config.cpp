#include "rpc_config.h"
#include <algorithm>

void TRPCConfig::AddSerialPort(const TSerialPortSettings& settings)
{
    Json::Value item;
    item["path"] = settings.Device;
    item["baud_rate"] = settings.BaudRate;
    item["data_bits"] = settings.DataBits;
    item["parity"] = std::string(1, settings.Parity);
    item["stop_bits"] = settings.StopBits;
    PortConfigs.append(std::move(item));
}

void TRPCConfig::AddTCPPort(const TTcpPortSettings& settings)
{
    Json::Value item;
    item["address"] = settings.Address;
    item["port"] = settings.Port;
    PortConfigs.append(std::move(item));
}

Json::Value TRPCConfig::GetPortConfigs() const
{
    return PortConfigs;
}
