#pragma once

#include <memory>
#include <mutex>
#include <string>

#include <wblib/rpc.h>

#include "rpc_fw_downloader.h"
#include "rpc_fw_update_state.h"
#include "rpc_fw_update_task.h"

class TFwRestoreTask: public ISerialClientTask
{
public:
    TFwRestoreTask(uint8_t slaveId,
                   const std::string& protocol,
                   const std::string& portPath,
                   const std::string& releaseSuite,
                   std::shared_ptr<TFwDownloader> downloader,
                   PFwUpdateState state,
                   PFwUpdateLock updateLock,
                   WBMQTT::TMqttRpcServer::TResultCallback onResult,
                   WBMQTT::TMqttRpcServer::TErrorCallback onError);

    ISerialClientTask::TRunResult Run(PFeaturePort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

private:
    uint8_t SlaveId;
    std::string Protocol;
    std::string PortPath;
    std::string ReleaseSuite;
    std::shared_ptr<TFwDownloader> Downloader;
    PFwUpdateState State;
    PFwUpdateLock UpdateLock;
    WBMQTT::TMqttRpcServer::TResultCallback OnResult;
    WBMQTT::TMqttRpcServer::TErrorCallback OnError;
};
