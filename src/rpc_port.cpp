#include "rpc_port.h"

TRPCPort::TRPCPort(PPort Port): Port(Port){};

PPort TRPCPort::GetPort()
{
    return this->Port;
}

bool TRPCPort::Compare(const PRPCPort other)
{
    return this->CompareByParameters(other);
}

TRPCSerialPort::TRPCSerialPort(PPort Port, std::string Path): TRPCPort(Port), Path(Path){};

bool TRPCSerialPort::CompareByParameters(const PRPCPort other)
{
    if (PRPCSerialPort p = std::dynamic_pointer_cast<TRPCSerialPort>(other)) {
        return this->Path == p->Path;
    }
    return false;
}

TRPCTCPPort::TRPCTCPPort(PPort Port, std::string Ip, uint16_t PortNumber)
    : TRPCPort(Port),
      Ip(Ip),
      PortNumber(PortNumber){};

bool TRPCTCPPort::CompareByParameters(const PRPCPort other)
{
    if (PRPCTCPPort p = std::dynamic_pointer_cast<TRPCTCPPort>(other)) {
        return ((this->Ip == p->Ip) && (this->PortNumber = p->PortNumber));
    }
    return false;
}