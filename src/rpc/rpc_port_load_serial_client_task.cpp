#include "rpc_port_load_serial_client_task.h"
#include "serial_exc.h"
#include "serial_port.h"

TRPCPortLoadSerialClientTask::TRPCPortLoadSerialClientTask(PRPCPortLoadRequest request): Request(request)
{
    Request = request;
    ExpireTime = std::chrono::steady_clock::now() + Request->TotalTimeout;
}

ISerialClientTask::TRunResult TRPCPortLoadSerialClientTask::Run(PPort port,
                                                                TSerialClientDeviceAccessHandler& lastAccessedDevice)
{
    if (std::chrono::steady_clock::now() > ExpireTime) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_REQUEST_TIMEOUT, "RPC request timeout");
        }
        return ISerialClientTask::TRunResult::OK;
    }

    try {
        port->CheckPortOpen();
        port->SkipNoise();
        port->SleepSinceLastInteraction(Request->FrameTimeout);
        lastAccessedDevice.PrepareToAccess(nullptr);

        TSerialPortSettingsGuard settingsGuard(port, Request->SerialPortSettings);

        port->WriteBytes(Request->Message);

        std::vector<uint8_t> response(Request->ResponseSize);
        size_t actualSize =
            port->ReadFrame(response.data(), Request->ResponseSize, Request->ResponseTimeout, Request->FrameTimeout)
                .Count;

        response.resize(actualSize);
        if (Request->OnResult) {
            Request->OnResult(response);
        }
    } catch (const TSerialDeviceException& error) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Port IO error: ") + error.what());
        }
    }
    return ISerialClientTask::TRunResult::OK;
}
