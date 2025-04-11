#pragma once
#include "rpc_config.h"
#include "rpc_exception.h"
#include "serial_driver.h"
#include <wblib/rpc.h>

struct TRPCPortDriver
{
    PSerialClient SerialClient;
    PRPCPort RPCPort;
};

typedef std::shared_ptr<TRPCPortDriver> PRPCPortDriver;

class TRPCPortDriverList
{
public:
    TRPCPortDriverList(PRPCConfig rpcConfig, PMQTTSerialDriver serialDriver);
    PRPCPortDriver Find(const Json::Value& request) const;
    PPort InitPort(const Json::Value& request);

private:
    std::vector<PRPCPortDriver> Drivers;
};

typedef std::shared_ptr<TRPCPortDriverList> PRPCPortDriverList;
