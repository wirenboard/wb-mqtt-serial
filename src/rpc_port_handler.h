#pragma once

#include "binary_semaphore.h"
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
    TRPCPortHandler();
    bool RPCTransieve(std::vector<uint8_t>& buf,
                      size_t responseSize,
                      std::chrono::microseconds respTimeout,
                      std::chrono::microseconds frameTimeout,
                      std::vector<uint8_t>& response,
                      size_t& actualResponseSize,
                      PBinarySemaphore rpcSemaphore,
                      PBinarySemaphoreSignal rpcSignal);
    void RPCRequestHandling(PPort Port);

private:
    std::mutex RPCMutex;
    std::vector<uint8_t> RPCWriteData, RPCReadData;
    size_t RPCRequestedSize, RPCActualSize;
    std::chrono::microseconds RPCRespTimeout;
    std::chrono::microseconds RPCFrameTimeout;
    RPCPortState RPCState = RPCPortState::RPC_IDLE;
    PBinarySemaphore semaphore;
    PBinarySemaphoreSignal signal;
};