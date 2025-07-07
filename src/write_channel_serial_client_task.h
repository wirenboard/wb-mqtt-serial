#pragma once

#include "register_handler.h"
#include "serial_client.h"

class TWriteChannelSerialClientTask: public ISerialClientTask
{

public:
    typedef std::function<void(PRegister reg)> TRegisterCallback;

    TWriteChannelSerialClientTask(PRegisterHandler handler,
                                  TRegisterCallback readCallback,
                                  TRegisterCallback errorCallback);

    ~TWriteChannelSerialClientTask() = default;

    ISerialClientTask::TRunResult Run(PPort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

private:
    PRegisterHandler Handler;
    TRegisterCallback ReadCallback;
    TRegisterCallback ErrorCallback;
};
