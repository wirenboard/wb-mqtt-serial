#include "write_channel_serial_client_task.h"

TWriteChannelSerialClientTask::TWriteChannelSerialClientTask(PRegisterHandler handler,
                                                             TRegisterCallback readCallback,
                                                             TRegisterCallback errorCallback)
    : Handler(handler),
      ReadCallback(readCallback),
      ErrorCallback(errorCallback)
{}

ISerialClientTask::TRunResult TWriteChannelSerialClientTask::Run(PPort port,
                                                                 TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                                                 const std::list<PSerialDevice>& polledDevices)
{
    if (!Handler->NeedToFlush()) {
        return ISerialClientTask::TRunResult::OK;
    }

    if (!port->IsOpen()) {
        Handler->Register()->SetError(TRegister::TError::WriteError);
        if (ErrorCallback) {
            ErrorCallback(Handler->Register());
        }
        return ISerialClientTask::TRunResult::OK;
    }

    if (lastAccessedDevice.PrepareToAccess(Handler->Register()->Device())) {
        Handler->Flush();
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
