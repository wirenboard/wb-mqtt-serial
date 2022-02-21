#pragma once

#include "port.h"
#include <mutex>
#include <wblib/rpc.h>

enum class RPCPortState
{
    RPC_IDLE,
    RPC_WRITE,
    RPC_READ,
    RPC_ERROR
};

class TRPCPortHandler
{
public:
    void RPCWrite(const std::vector<uint8_t>& buf,
                  size_t responseSize,
                  std::chrono::milliseconds respTimeout,
                  std::chrono::milliseconds frameTimeout);
    bool RPCRead(std::vector<uint8_t>& buf, size_t& actualSize, bool& error);
    void RPCRequestHandling(PPort Port);

private:
    std::mutex RPCMutex;
    std::vector<uint8_t> RPCWriteData, RPCReadData;
    size_t RPCRequestedSize, RPCActualSize;
    std::chrono::milliseconds RPCRespTimeout;
    std::chrono::milliseconds RPCFrameTimeout;
    RPCPortState RPCState = RPCPortState::RPC_IDLE;
};