#pragma once

#include "binary_semaphore.h"
#include "port.h"
#include "rpc_request.h"
#include <condition_variable>
#include <mutex>
#include <wblib/rpc.h>

enum class RPCRequestState
{
    RPC_IDLE,
    RPC_PENDING,
    RPC_COMPLETE,
    RPC_ERROR
};

class TRPCRequestHandler
{
public:
    std::vector<uint8_t> RPCTransceive(PRPCRequest request,
                                       PBinarySemaphore serialClientSemaphore,
                                       PBinarySemaphoreSignal serialClientSignal);
    void RPCRequestHandling(PPort port);

private:
    std::mutex Mutex;
    std::condition_variable RequestExecution;
    PRPCRequest Request;
    std::vector<uint8_t> ReadData;
    RPCRequestState State = RPCRequestState::RPC_IDLE;
    std::string ErrorMessage;
};

typedef std::shared_ptr<TRPCRequestHandler> PRPCRequestHandler;
