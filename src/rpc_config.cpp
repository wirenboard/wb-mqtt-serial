#include "rpc_config.h"
#include <algorithm>

void TRPCConfig::AddSerialPort(PPort port, const TSerialPortSettings& settings)
{
    auto existedPort =
        std::find_if(Ports.begin(), Ports.end(), [&port](PRPCPort RPCPort) { return port == RPCPort->GetPort(); });

    if (existedPort == Ports.end()) {
        PRPCPort newRPCPort = std::make_shared<TRPCSerialPort>(port, settings.Device);
        Ports.push_back(newRPCPort);

        Json::Value item;
        item["path"] = settings.Device;
        item["baud_rate"] = settings.BaudRate;
        item["data_bits"] = settings.DataBits;
        item["parity"] = std::string(1, settings.Parity);
        item["stop_bits"] = settings.StopBits;
        PortConfigs.append(std::move(item));
    }
}

void TRPCConfig::AddTCPPort(PPort port, const TTcpPortSettings& settings)
{
    auto existedPort =
        std::find_if(Ports.begin(), Ports.end(), [&port](PRPCPort RPCPort) { return port == RPCPort->GetPort(); });

    if (existedPort == Ports.end()) {
        PRPCPort newRPCPort = std::make_shared<TRPCTCPPort>(port, settings.Address, settings.Port);
        Ports.push_back(newRPCPort);

        Json::Value item;
        item["address"] = settings.Address;
        item["port"] = settings.Port;
        PortConfigs.append(std::move(item));
    }
}

std::vector<PRPCPort> TRPCConfig::GetPorts()
{
    return Ports;
}

Json::Value TRPCConfig::GetPortConfigs() const
{
    return PortConfigs;
}
