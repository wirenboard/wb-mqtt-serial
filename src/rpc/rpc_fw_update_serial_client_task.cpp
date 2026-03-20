#include "rpc_fw_update_serial_client_task.h"
#include "log.h"
#include "port/port.h"
#include "rpc_fw_update_helpers.h"
#include "rpc_helpers.h"
#include "serial_exc.h"

#define LOG(logger) ::logger.Log() << "[fw-update] "

TFwUpdateSerialClientTask::TFwUpdateSerialClientTask(uint8_t slaveId,
                                                     const std::string& protocol,
                                                     const std::string& softwareType,
                                                     const std::string& portPath,
                                                     const std::string& releaseSuite,
                                                     const TSerialPortConnectionSettings& portSettings,
                                                     std::shared_ptr<TFwDownloader> downloader,
                                                     PFwUpdateState state,
                                                     PFwUpdateLock updateLock,
                                                     WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                                     WBMQTT::TMqttRpcServer::TErrorCallback onError)
    : SlaveId(slaveId),
      Protocol(protocol),
      SoftwareType(softwareType),
      PortPath(portPath),
      ReleaseSuite(releaseSuite),
      PortSettings(portSettings),
      Downloader(std::move(downloader)),
      State(std::move(state)),
      UpdateLock(std::move(updateLock)),
      OnResult(std::move(onResult)),
      OnError(std::move(onError))
{}

ISerialClientTask::TRunResult TFwUpdateSerialClientTask::Run(PFeaturePort port,
                                                             TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                                             const std::list<PSerialDevice>& polledDevices)
{
    try {
        if (!port->IsOpen()) {
            port->Open();
        }
        lastAccessedDevice.PrepareToAccess(*port, nullptr);
        TSerialPortSettingsGuard settingsGuard(port, PortSettings);
        port->SkipNoise();

        auto traits = MakeModbusTraits(Protocol);
        auto info = ReadFwDeviceInfo(*traits, *port, SlaveId);

        // Send RPC response early — flash proceeds asynchronously from client's perspective
        if (OnResult) {
            Json::Value result;
            result = "Ok";
            OnResult(result);
        }

        try {
            if (SoftwareType == "firmware") {
                DoFirmwareUpdate(*port, *traits, info);
            } else if (SoftwareType == "bootloader") {
                DoBootloaderUpdate(*port, *traits, info);
            } else if (SoftwareType == "component") {
                DoComponentsUpdate(*port, *traits, info);
            } else {
                throw std::runtime_error("Unknown software type: " + SoftwareType);
            }
        } catch (const std::exception& e) {
            LOG(Error) << "Firmware update error: " << e.what();
            std::string errorId = "com.wb.device_manager.generic_error";
            std::string errorMsg = "Internal error. Check logs for more info";
            Json::Value metadata;
            metadata["exception"] = e.what();
            State->SetError(SlaveId, PortPath, SoftwareType, errorId, errorMsg, metadata);
        }
        {
            std::lock_guard<std::mutex> lock(UpdateLock->Mutex);
            UpdateLock->InProgress = false;
        }
    } catch (const TResponseTimeoutException& e) {
        {
            std::lock_guard<std::mutex> lock(UpdateLock->Mutex);
            UpdateLock->InProgress = false;
        }
        if (OnError) {
            OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Device not responding: ") + e.what());
        }
    } catch (const std::exception& e) {
        {
            std::lock_guard<std::mutex> lock(UpdateLock->Mutex);
            UpdateLock->InProgress = false;
        }
        if (OnError) {
            OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Error starting firmware update: ") + e.what());
        }
    }
    return ISerialClientTask::TRunResult::OK;
}

// Cf. firmware_update.py:785 FirmwareUpdater.update_software() — firmware branch
void TFwUpdateSerialClientTask::DoFirmwareUpdate(TPort& port, Modbus::IModbusTraits& traits, const TFwDeviceInfo& info)
{
    auto released = Downloader->GetReleasedFirmware(info.FwSignature, ReleaseSuite);
    DoFlash(port,
            traits,
            "firmware",
            info.FwVersion,
            released.Version,
            released.Endpoint,
            true,
            info.CanPreservePortSettings);

    // Also update components after firmware
    DoComponentsUpdate(port, traits, info);
}

// Cf. firmware_update.py:939 _update_bootloader()
void TFwUpdateSerialClientTask::DoBootloaderUpdate(TPort& port,
                                                   Modbus::IModbusTraits& traits,
                                                   const TFwDeviceInfo& info)
{
    auto bootloader = Downloader->GetLatestBootloader(info.FwSignature);
    DoFlash(port,
            traits,
            "bootloader",
            info.BootloaderVersion,
            bootloader.Version,
            bootloader.Endpoint,
            true,
            info.CanPreservePortSettings);

    // Auto-restore firmware after bootloader update (device stays in bootloader mode).
    // Give the device time to settle after rebooting into the bootloader before starting the flash.
    // Cf. firmware_update.py:973
    LOG(Info) << "Auto-restoring firmware after bootloader update for slave " << static_cast<int>(SlaveId);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    auto released = Downloader->GetReleasedFirmware(info.FwSignature, ReleaseSuite);
    DoFlash(port,
            traits,
            "firmware",
            "",
            released.Version,
            released.Endpoint,
            false, // already in bootloader
            false);
}

// Cf. firmware_update.py:538 update_components()
void TFwUpdateSerialClientTask::DoComponentsUpdate(TPort& port,
                                                   Modbus::IModbusTraits& traits,
                                                   const TFwDeviceInfo& info)
{
    for (const auto& comp: info.Components) {
        try {
            auto released = Downloader->GetReleasedFirmware(comp.Signature, ReleaseSuite);
            if (ComponentFirmwareIsNewer(comp.FwVersion, released.Version)) {
                DoFlash(port,
                        traits,
                        "component",
                        comp.FwVersion,
                        released.Version,
                        released.Endpoint,
                        false, // components don't reboot to bootloader
                        false,
                        comp.Number,
                        comp.Model);
            }
        } catch (const std::exception& e) {
            LOG(Warn) << "Cannot update component " << comp.Number << ": " << e.what();
        }
    }
}

void TFwUpdateSerialClientTask::DoFlash(TPort& port,
                                        Modbus::IModbusTraits& traits,
                                        const std::string& type,
                                        const std::string& fromVersion,
                                        const std::string& toVersion,
                                        const std::string& fwUrl,
                                        bool reboot,
                                        bool canPreserve,
                                        int componentNumber,
                                        const std::string& componentModel)
{
    // Update state to show 0% progress
    TDeviceUpdateInfo updateInfo;
    updateInfo.PortPath = PortPath;
    updateInfo.Protocol = Protocol;
    updateInfo.SlaveId = SlaveId;
    updateInfo.ToVersion = toVersion;
    updateInfo.Progress = 0;
    updateInfo.FromVersion = fromVersion;
    updateInfo.Type = type;
    updateInfo.ComponentNumber = componentNumber;
    updateInfo.ComponentModel = componentModel;
    State->Update(updateInfo);

    // Download and parse firmware file
    auto firmware = Downloader->DownloadAndParseWBFW(fwUrl);

    // Flash with throttled progress updates
    TUpdateNotifier notifier(30);
    FlashFirmware(traits, port, SlaveId, firmware, reboot, canPreserve, [&](int percent) {
        if (notifier.ShouldNotify(percent)) {
            updateInfo.Progress = percent;
            State->Update(updateInfo);
        }
    });

    // Success — remove from state
    State->Remove(SlaveId, PortPath, type);
}
