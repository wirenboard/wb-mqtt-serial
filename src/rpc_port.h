#pragma once
#include "port.h"

enum class TRPCPortMode
{
    RPC_RTU,
    RPC_TCP
};

class TRPCPort
{
public:
    PPort Port;
    enum TRPCPortMode Mode;
    std::string Path, Ip;
    int PortNumber;
};

typedef std::shared_ptr<TRPCPort> PRPCPort;