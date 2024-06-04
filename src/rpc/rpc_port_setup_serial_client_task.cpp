#include "rpc_port_setup_serial_client_task.h"
#include "modbus_common.h"
#include "serial_exc.h"
#include "serial_port.h"

TRPCPortSetupSerialClientTask::TRPCPortSetupSerialClientTask(PRPCPortSetupRequest request): Request(request)
{
    Request = request;
    ExpireTime = std::chrono::steady_clock::now() + Request->TotalTimeout;
}

ISerialClientTask::TRunResult TRPCPortSetupSerialClientTask::Run(PPort port,
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
        for (auto item: Request->Items) {
            TSerialPortSettingsGuard settingsGuard(port, item.SerialPortSettings);
            auto frameTimeout = std::chrono::ceil<std::chrono::milliseconds>(port->GetSendTimeBytes(3.5));
            port->SleepSinceLastInteraction(frameTimeout);
            lastAccessedDevice.PrepareToAccess(nullptr);

            Modbus::TModbusRTUTraitsFactory traitsFactory;
            auto traits = traitsFactory.GetModbusTraits(port, false);
            Modbus::TRegisterCache cache;
            Modbus::WriteSetupRegisters(*traits,
                                        *port,
                                        item.SlaveId,
                                        item.Regs,
                                        cache,
                                        std::chrono::microseconds(0),
                                        std::chrono::milliseconds(1),
                                        frameTimeout);
        }

        if (Request->OnResult) {
            Request->OnResult();
        }
    } catch (const TSerialDeviceException& error) {
        if (Request->OnError) {
            Request->OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Port IO error: ") + error.what());
        }
    }
    return ISerialClientTask::TRunResult::OK;
}
