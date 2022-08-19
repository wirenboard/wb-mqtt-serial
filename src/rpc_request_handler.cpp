#include "rpc_request_handler.h"
#include "rpc_handler.h"
#include "serial_exc.h"

std::vector<uint8_t> TRPCRequestHandler::RPCTransceive(PRPCRequest request,
                                                       PBinarySemaphore serialClientSemaphore,
                                                       PBinarySemaphoreSignal serialClientSignal)
{
    std::unique_lock<std::mutex> lock(Mutex);
    Request = request;
    State = RPCRequestState::RPC_PENDING;
    auto now = std::chrono::steady_clock::now();
    auto until = now + Request->TotalTimeout;
    serialClientSemaphore->Signal(serialClientSignal);

    if (!RequestExecution.wait_until(lock, until, [this]() {
            return (State == RPCRequestState::RPC_COMPLETE) || (State == RPCRequestState::RPC_ERROR);
        }))
    {
        State = RPCRequestState::RPC_IDLE;
        throw TRPCException("Request handler is not responding", TRPCResultCode::RPC_WRONG_TIMEOUT);
    }

    RPCRequestState lastRequestState = State;
    State = RPCRequestState::RPC_IDLE;
    if (lastRequestState == RPCRequestState::RPC_COMPLETE) {
        return ReadData;
    } else {
        throw TRPCException("Port IO error", TRPCResultCode::RPC_WRONG_IO);
    }
}

void TRPCRequestHandler::RPCRequestHandling(PPort port)
{
    std::lock_guard<std::mutex> lock(Mutex);
    if (State == RPCRequestState::RPC_PENDING) {
        try {
            port->CheckPortOpen();
            port->SleepSinceLastInteraction(Request->FrameTimeout);
            port->WriteBytes(Request->Message);

            std::vector<uint8_t> readData;
            readData.reserve(Request->ResponseSize);
            size_t actualSize = port->ReadFrame(readData.data(),
                                                Request->ResponseSize,
                                                Request->ResponseTimeout,
                                                Request->FrameTimeout);

            ReadData.clear();
            std::copy_n(readData.begin(), actualSize, std::back_inserter(ReadData));
            State = RPCRequestState::RPC_COMPLETE;
        } catch (TSerialDeviceException error) {
            State = RPCRequestState::RPC_ERROR;
        }

        RequestExecution.notify_all();
    }
}
