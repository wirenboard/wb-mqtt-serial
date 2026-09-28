#pragma once

#include <map>

#include <wblib/rpc.h>

#include "rpc_fw_downloader.h"
#include "rpc_fw_update_state.h"
#include "rpc_fw_update_task.h"
#include "rpc_port_settings.h"

class TFwUpdateSerialClientTask: public ISerialClientTask
{
public:
    TFwUpdateSerialClientTask(uint8_t slaveId,
                              const std::string& protocol,
                              EFwSoftwareType softwareType,
                              const std::string& releaseSuite,
                              const TRPCPortSettings& portSettings,
                              std::shared_ptr<TFwDownloader> downloader,
                              PFwUpdateState state,
                              PFwUpdateLock updateLock,
                              WBMQTT::TMqttRpcServer::TResultCallback onResult,
                              WBMQTT::TMqttRpcServer::TErrorCallback onError);

    ISerialClientTask::TRunResult Run(PFeaturePort port,
                                      TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                      const std::list<PSerialDevice>& polledDevices) override;

private:
    void ReleaseLock();
    void ReadReleasedSoftware(const TFwDeviceInfo& info);
    void DoFirmwareUpdate(TPort& port, Modbus::IModbusTraits& traits, const TFwDeviceInfo& info);
    void DoBootloaderUpdate(TPort& port, Modbus::IModbusTraits& traits, const TFwDeviceInfo& info);
    void DoComponentsUpdate(TPort& port, Modbus::IModbusTraits& traits, const TFwDeviceInfo& info);
    void DoFlash(TPort& port,
                 Modbus::IModbusTraits& traits,
                 EFwSoftwareType type,
                 const std::string& fromVersion,
                 const std::string& toVersion,
                 const std::string& fwUrl,
                 bool reboot,
                 bool canPreserve,
                 int componentNumber = -1,
                 const std::string& componentModel = "");

    uint8_t SlaveId;
    std::string Protocol;
    EFwSoftwareType SoftwareType;
    std::string ReleaseSuite;
    TRPCPortSettings PortSettings;
    std::shared_ptr<TFwDownloader> Downloader;
    TReleasedBinary ReleasedFirmware;
    TReleasedBinary ReleasedBootloader;
    std::map<int, TReleasedBinary> ReleasedComponents;
    PFwUpdateState State;
    PFwUpdateLock UpdateLock;
    WBMQTT::TMqttRpcServer::TResultCallback OnResult;
    WBMQTT::TMqttRpcServer::TErrorCallback OnError;
};
