#pragma once

#include <wblib/json_utils.h>
#include <wblib/rpc.h>

#include "serial_device.h"
#include "serial_port_settings.h"

class TRPCPortSetupRequestItem
{
public:
    uint8_t SlaveId;
    std::optional<uint32_t> Sn;
    TSerialPortConnectionSettings SerialPortSettings;
    std::vector<PDeviceSetupItem> Regs;
};

class TRPCPortSetupRequest
{
public:
    std::vector<TRPCPortSetupRequestItem> Items;

    std::chrono::milliseconds TotalTimeout;

    std::function<void()> OnResult = nullptr;
    WBMQTT::TMqttRpcServer::TErrorCallback OnError = nullptr;
};

typedef std::shared_ptr<TRPCPortSetupRequest> PRPCPortSetupRequest;
