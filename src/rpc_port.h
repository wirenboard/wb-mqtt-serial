#pragma once
#include "port.h"
#include "wblib/json_utils.h"

class TRPCPort
{
public:
    TRPCPort(PPort Port);
    virtual ~TRPCPort() = default;
    PPort GetPort();
    virtual bool Match(const Json::Value& Request) const = 0;

protected:
    PPort Port;
};

typedef std::shared_ptr<TRPCPort> PRPCPort;

class TRPCSerialPort: public TRPCPort
{
public:
    TRPCSerialPort(PPort Port, const std::string& Path);
    bool Match(const Json::Value& Request) const;

protected:
    std::string Path;
};
typedef std::shared_ptr<TRPCSerialPort> PRPCSerialPort;

class TRPCTCPPort: public TRPCPort
{
public:
    TRPCTCPPort(PPort Port, const std::string& Ip, uint16_t PortNumber);
    bool Match(const Json::Value& Request) const;

protected:
    std::string Ip;
    uint16_t PortNumber;
};
typedef std::shared_ptr<TRPCTCPPort> PRPCTCPPort;