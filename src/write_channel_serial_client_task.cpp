#include "write_channel_serial_client_task.h"
#include "log.h"

#define LOG(logger) logger.Log() << "[serial client] "

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

    if (!port->IsOpen() || !Handler->Register()->IsSupported() ||
        Handler->Register()->Device()->GetConnectionState() == TDeviceConnectionState::DISCONNECTED)
    {
        Handler->Register()->SetError(TRegister::TError::WriteError);
        if (ErrorCallback) {
            ErrorCallback(Handler->Register());
        }
        std::string error;
        if (!port->IsOpen()) {
            error = "port is not open";
        } else if (!Handler->Register()->IsSupported()) {
            error = "register is not supporgted by device firmware";
        } else {
            error = "device is disconnected";
        }
        LOG(Warn) << Handler->Register()->ToString() << " register write cancelled: " << error;
        return ISerialClientTask::TRunResult::OK;
    }

    if (lastAccessedDevice.PrepareToAccess(*port, Handler->Register()->Device())) {
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
    return Handler->NeedToFlush() ? ISerialClientTask::TRunResult::RETRY : ISerialClientTask::TRunResult::OK;
}
