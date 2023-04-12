#pragma once

#include "binary_semaphore.h"
#include "port.h"
#include "rpc_request.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <wblib/rpc.h>

enum class RPCRequestState
{
    RPC_IDLE,
    RPC_PENDING
};

class TRPCRequestHandler
{
public:
    void RPCTransceive(PRPCRequest request,
                       PBinarySemaphore serialClientSemaphore,
                       PBinarySemaphoreSignal serialClientSignal);
    void RPCRequestHandling(PPort port);

private:
    std::mutex Mutex;
    PRPCRequest Request;
    RPCRequestState State = RPCRequestState::RPC_IDLE;
    std::chrono::steady_clock::time_point ExpireTime;
};

typedef std::shared_ptr<TRPCRequestHandler> PRPCRequestHandler;
