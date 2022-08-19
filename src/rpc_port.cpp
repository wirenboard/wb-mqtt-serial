#include "rpc_port.h"

TRPCPort::TRPCPort(PPort Port): Port(Port){};

PPort TRPCPort::GetPort()
{
    return this->Port;
}

TRPCSerialPort::TRPCSerialPort(PPort Port, const std::string& Path): TRPCPort(Port), Path(Path){};

bool TRPCSerialPort::Match(const Json::Value& Request) const
{
    std::string Path;
    return WBMQTT::JSON::Get(Request, "path", Path) && (Path == this->Path);
}

TRPCTCPPort::TRPCTCPPort(PPort Port, const std::string& Ip, uint16_t PortNumber)
    : TRPCPort(Port),
      Ip(Ip),
      PortNumber(PortNumber){};

bool TRPCTCPPort::Match(const Json::Value& Request) const
{
    std::string Ip;
    int PortNumber;
    return WBMQTT::JSON::Get(Request, "ip", Ip) && WBMQTT::JSON::Get(Request, "port", PortNumber) && (Ip == this->Ip) &&
           (PortNumber == this->PortNumber);
}