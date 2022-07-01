#include "rpc_port_handler.h"
#include "binary_semaphore.h"
#include "serial_exc.h"

TRPCPortHandler::TRPCPortHandler()
{
    Semaphore = std::make_shared<TBinarySemaphore>();
    Signal = Semaphore->SignalRegistration();
}

bool TRPCPortHandler::RPCTransceive(PRPCRequest request,
                                    std::vector<uint8_t>& response,
                                    size_t& actualResponseSize,
                                    PBinarySemaphore rpcSemaphore,
                                    PBinarySemaphoreSignal rpcSignal)
{
    RPCMutex.lock();

    RPCWriteData = request->Msg;
    RPCRequestedSize = request->ResponseSize;
    RPCRespTimeout = request->ResponseTimeout;
    RPCFrameTimeout = request->FrameTimeout;
    RPCState = RPCPortState::RPC_WRITE;
    auto now = std::chrono::steady_clock::now();
    auto until = now + request->TotalTimeout;
    rpcSemaphore->Signal(rpcSignal);

    RPCMutex.unlock();

    if (!Semaphore->Wait(until)) {
        RPCState = RPCPortState::RPC_IDLE;
        return false;
    }

    Semaphore->ResetAllSignals();

    bool result;
    if (RPCState == RPCPortState::RPC_READ) {
        response = RPCReadData;
        actualResponseSize = RPCActualSize;
        result = true;
    } else {
        result = false;
    }

    RPCState = RPCPortState::RPC_IDLE;
    return result;
}

void TRPCPortHandler::RPCRequestHandling(PPort port)
{
    if (RPCState == RPCPortState::RPC_WRITE) {
        RPCMutex.lock();
        try {
            port->SleepSinceLastInteraction(RPCFrameTimeout);
            port->WriteBytes(RPCWriteData);

            // uint8_t readData[RPCRequestedSize];
            std::vector<uint8_t> readData;
            readData.reserve(RPCRequestedSize);
            RPCActualSize = port->ReadFrame(readData.data(), RPCRequestedSize, RPCRespTimeout, RPCFrameTimeout);

            RPCReadData.clear();
            for (size_t i = 0; i < RPCRequestedSize; i++) {
                RPCReadData.push_back(readData[i]);
            }
            RPCState = RPCPortState::RPC_READ;
        } catch (TSerialDeviceException error) {
            RPCState = RPCPortState::RPC_ERROR;
        }

        Semaphore->Signal(Signal);
        RPCMutex.unlock();
    }
}