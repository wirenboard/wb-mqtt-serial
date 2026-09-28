#pragma once

#include <memory>
#include <string>

#include <wblib/rpc.h>

#include "rpc_fw_downloader.h"
#include "rpc_fw_update_task.h"
#include "rpc_port_settings.h"

class TFwGetFirmwareInfoTask: public ISerialClientTask
{
public:
    TFwGetFirmwareInfoTask(uint8_t slaveId,
                           const std::string& protocol,
                           const std::string& releaseSuite,
                           const TRPCPortSettings& portSettings,
                           std::shared_ptr<TFwDownloader> downloader,
                           WBMQTT::TMqttRpcServer::TResultCallback onResult,
                           WBMQTT::TMqttRpcServer::TErrorCallback onError);

    ISerialClientTask::TRunResult Run(PFeaturePort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

private:
    uint8_t SlaveId;
    std::string Protocol;
    std::string ReleaseSuite;
    TRPCPortSettings PortSettings;
    std::shared_ptr<TFwDownloader> Downloader;
    WBMQTT::TMqttRpcServer::TResultCallback OnResult;
    WBMQTT::TMqttRpcServer::TErrorCallback OnError;
};
