#pragma once

#include "binary_semaphore.h"
#include "port.h"
#include "rpc_request.h"
#include <mutex>
#include <wblib/rpc.h>

enum class RPCRequestState
{
    RPC_IDLE,
    RPC_WRITE,
    RPC_READ,
    RPC_ERROR
};

class TRPCRequestHandler
{
public:
    TRPCRequestHandler();
    std::vector<uint8_t> RPCTransceive(PRPCRequest Request,
                                       PBinarySemaphore SerialClientSemaphore,
                                       PBinarySemaphoreSignal SerialClientSignal);
    void RPCRequestHandling(PPort Port);

private:
    std::mutex Mutex;
    PRPCRequest Request;
    std::vector<uint8_t> ReadData;
    RPCRequestState State = RPCRequestState::RPC_IDLE;
    PBinarySemaphore Semaphore;
    PBinarySemaphoreSignal Signal;
};

typedef std::shared_ptr<TRPCRequestHandler> PRPCRequestHandler;
