#pragma once
#include "port.h"

class TRPCPort
{
public:
    TRPCPort(PPort Port);
    PPort GetPort();
    bool Compare(const std::shared_ptr<TRPCPort> other);

protected:
    PPort Port;
    virtual bool CompareByParameters(const std::shared_ptr<TRPCPort> other) = 0;
};

typedef std::shared_ptr<TRPCPort> PRPCPort;

class TRPCSerialPort: public TRPCPort
{
public:
    TRPCSerialPort(PPort Port, std::string Path);

protected:
    std::string Path;
    virtual bool CompareByParameters(const PRPCPort other);
};
typedef std::shared_ptr<TRPCSerialPort> PRPCSerialPort;

class TRPCTCPPort: public TRPCPort
{
public:
    TRPCTCPPort(PPort Port, std::string Ip, uint16_t PortNumber);

protected:
    std::string Ip;
    uint16_t PortNumber;
    virtual bool CompareByParameters(const PRPCPort other);
};
typedef std::shared_ptr<TRPCTCPPort> PRPCTCPPort;