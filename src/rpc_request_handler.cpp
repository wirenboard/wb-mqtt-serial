#include "rpc_request_handler.h"
#include "rpc_handler.h"
#include "serial_exc.h"
#include "serial_port.h"

void TRPCRequestHandler::RPCTransceive(PRPCRequest request,
                                       PBinarySemaphore serialClientSemaphore,
                                       PBinarySemaphoreSignal serialClientSignal)
{
    std::unique_lock<std::mutex> lock(Mutex);
    Request = request;
    State = RPCRequestState::RPC_PENDING;
    serialClientSemaphore->Signal(serialClientSignal);
}

void TRPCRequestHandler::RPCRequestHandling(PPort port)
{
    std::lock_guard<std::mutex> lock(Mutex);
    if (State == RPCRequestState::RPC_PENDING) {
        try {
            port->CheckPortOpen();
            port->SkipNoise();
            port->SleepSinceLastInteraction(Request->FrameTimeout);

            TSerialPortSettingsGuard settingsGuard(port, Request->SerialPortSettings);

            port->WriteBytes(Request->Message);

            std::vector<uint8_t> response(Request->ResponseSize);
            size_t actualSize = port->ReadFrame(response.data(),
                                                Request->ResponseSize,
                                                Request->ResponseTimeout,
                                                Request->FrameTimeout);

            response.resize(actualSize);
            State = RPCRequestState::RPC_COMPLETE;
            if (Request->OnResult) {
                Request->OnResult(response);
            }
        } catch (const TSerialDeviceException& error) {
            State = RPCRequestState::RPC_ERROR;
            if (Request->OnError) {
                Request->OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Port IO error: ") + error.what());
            }
        }

        State = RPCRequestState::RPC_IDLE;
    }
}
