#pragma once

#include <memory>
#include <string>

#include <wblib/rpc.h>

#include "rpc_fw_downloader.h"
#include "rpc_fw_update_task.h"

class TFwGetFirmwareInfoTask: public ISerialClientTask
{
public:
    TFwGetFirmwareInfoTask(uint8_t slaveId,
                           const std::string& protocol,
                           std::shared_ptr<TFwDownloader> downloader,
                           const std::string& releaseSuite,
                           WBMQTT::TMqttRpcServer::TResultCallback onResult,
                           WBMQTT::TMqttRpcServer::TErrorCallback onError);

    ISerialClientTask::TRunResult Run(PFeaturePort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

private:
    uint8_t SlaveId;
    std::string Protocol;
    std::shared_ptr<TFwDownloader> Downloader;
    std::string ReleaseSuite;
    WBMQTT::TMqttRpcServer::TResultCallback OnResult;
    WBMQTT::TMqttRpcServer::TErrorCallback OnError;
};
