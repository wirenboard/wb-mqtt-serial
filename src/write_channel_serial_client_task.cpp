#include "write_channel_serial_client_task.h"
#include "log.h"

#define LOG(logger) logger.Log() << "[serial client] "

using namespace std::chrono_literals;

namespace
{
    // A device which is not polled can be reconnected only by writing to it,
    // but writes retried in every cycle occupy the port and slow down other requests,
    // so a disconnected device is accessed not more often than once per interval
    const auto DISCONNECTED_WRITE_INTERVAL = 5s;
}

TWriteChannelSerialClientTask::TWriteChannelSerialClientTask(PRegisterHandler handler,
                                                             TRegisterCallback readCallback,
                                                             TRegisterCallback errorCallback)
    : Handler(handler),
      ReadCallback(readCallback),
      ErrorCallback(errorCallback)
{}

ISerialClientTask::TRunResult TWriteChannelSerialClientTask::Run(PFeaturePort port,
                                                                 TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                                                 const std::list<PSerialDevice>& polledDevices)
{
    if (!Handler->NeedToFlush()) {
        return ISerialClientTask::TRunResult::OK;
    }

    auto device = Handler->Register()->Device();
    auto isDisconnected = device->GetConnectionState() == TDeviceConnectionState::DISCONNECTED;
    auto skipWrite =
        std::chrono::steady_clock::now() - device->GetLastWriteTime() < DISCONNECTED_WRITE_INTERVAL;
    if (!port->IsOpen() || !Handler->Register()->IsSupported() || (isDisconnected && skipWrite)) {
        Handler->Register()->SetError(TRegister::TError::WriteError);
        if (ErrorCallback) {
            ErrorCallback(Handler->Register());
        }
        auto retry = true;
        std::string error;
        if (!port->IsOpen()) {
            error = "port is not open";
        } else if (!Handler->Register()->IsSupported()) {
            retry = false;
            error = "register is not supported by device firmware";
        } else {
            error = "device is disconnected";
        }
        if (retry && Handler->NeedRetryAfterWriteFail()) {
            LOG(Debug) << Handler->Register()->ToString() << " register write deferred: " << error;
            return ISerialClientTask::TRunResult::RETRY;
        }
        LOG(Warn) << Handler->Register()->ToString() << " register write cancelled: " << error;
        return ISerialClientTask::TRunResult::OK;
    }

    if (lastAccessedDevice.PrepareToAccess(*port, device)) {
        Handler->Flush(*port);
    } else {
        Handler->Register()->SetError(TRegister::TError::WriteError);
    }
    if (Handler->Register()->GetErrorState().test(TRegister::TError::WriteError)) {
        if (ErrorCallback) {
            ErrorCallback(Handler->Register());
        }
    } else {
        if (ReadCallback) {
            ReadCallback(Handler->Register());
        }
    }

    device->SetLastWriteTime(std::chrono::steady_clock::now());
    return Handler->NeedToFlush() ? ISerialClientTask::TRunResult::RETRY : ISerialClientTask::TRunResult::OK;
}
