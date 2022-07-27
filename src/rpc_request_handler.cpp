#include "rpc_request_handler.h"
#include "binary_semaphore.h"
#include "rpc_handler.h"
#include "serial_exc.h"

TRPCRequestHandler::TRPCRequestHandler()
{
    Semaphore = std::make_shared<TBinarySemaphore>();
    Signal = Semaphore->MakeSignal();
    Semaphore->ResetAllSignals();
}

std::vector<uint8_t> TRPCRequestHandler::RPCTransceive(PRPCRequest Request,
                                                       PBinarySemaphore SerialClientSemaphore,
                                                       PBinarySemaphoreSignal SerialClientSignal)
{
    Mutex.lock();
    this->Request = Request;
    State = RPCRequestState::RPC_WRITE;
    auto now = std::chrono::steady_clock::now();
    auto until = now + Request->TotalTimeout; // + std::chrono::seconds();
    SerialClientSemaphore->Signal(SerialClientSignal);

    Mutex.unlock();

    std::vector<uint8_t> Response;

    if (!Semaphore->Wait(until)) {
        State = RPCRequestState::RPC_IDLE;
        throw TRPCException("Request handler is not responding", TRPCResultCode::RPC_WRONG_TIMEOUT);
    }

    Semaphore->ResetAllSignals();

    RPCRequestState lastRequestState = State;
    State = RPCRequestState::RPC_IDLE;
    if (lastRequestState == RPCRequestState::RPC_READ) {
        return ReadData;
    } else {
        throw TRPCException("Port IO error", TRPCResultCode::RPC_WRONG_IO);
    }
}

void TRPCRequestHandler::RPCRequestHandling(PPort Port)
{
    if (State == RPCRequestState::RPC_WRITE) {
        std::lock_guard<std::mutex> lock(Mutex);
        try {
            Port->CheckPortOpen();
            Port->SleepSinceLastInteraction(Request->FrameTimeout);
            Port->WriteBytes(Request->Message);

            std::vector<uint8_t> readData;
            readData.reserve(Request->ResponseSize);
            size_t ActualSize = Port->ReadFrame(readData.data(),
                                                Request->ResponseSize,
                                                Request->ResponseTimeout,
                                                Request->FrameTimeout);

            ReadData.clear();
            for (size_t i = 0; i < ActualSize; i++) {
                ReadData.push_back(readData[i]);
            }
            State = RPCRequestState::RPC_READ;
        } catch (TSerialDeviceException error) {
            State = RPCRequestState::RPC_ERROR;
        }

        Semaphore->Signal(Signal);
    }
}
