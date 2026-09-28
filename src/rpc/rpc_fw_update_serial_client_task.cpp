#include "rpc_fw_update_serial_client_task.h"
#include "log.h"
#include "port/port.h"
#include "rpc_fw_update_helpers.h"
#include "rpc_helpers.h"
#include "serial_exc.h"

#define LOG(logger) ::logger.Log() << "[fw-update] "

TFwUpdateSerialClientTask::TFwUpdateSerialClientTask(uint8_t slaveId,
                                                     const std::string& protocol,
                                                     EFwSoftwareType softwareType,
                                                     const std::string& releaseSuite,
                                                     const TRPCPortSettings& portSettings,
                                                     std::shared_ptr<TFwDownloader> downloader,
                                                     PFwUpdateState state,
                                                     PFwUpdateLock updateLock,
                                                     WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                                     WBMQTT::TMqttRpcServer::TErrorCallback onError)
    : SlaveId(slaveId),
      Protocol(protocol),
      SoftwareType(softwareType),
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
        TSerialPortSettingsGuard settingsGuard(port, GetRPCPortConnectionSettings(PortSettings));
        port->SkipNoise();

        auto traits = MakeModbusTraits(Protocol);
        auto info = ReadFwDeviceInfo(*traits, *port, SlaveId);

        // Components are flashed without rebooting to a bootloader, so their update is always possible
        if (SoftwareType != EFwSoftwareType::Component &&
            RequiresDefaultPortSettings(std::holds_alternative<TRPCTcpPortSettings>(PortSettings),
                                        Protocol,
                                        info.CanPreservePortSettings) &&
            !HasDefaultPortSettings(*traits, *port, SlaveId))
        {
            ReleaseLock();
            if (OnError) {
                OnError(WBMQTT::E_RPC_SERVER_ERROR, "Can't update firmware over TCP");
            }
            return ISerialClientTask::TRunResult::OK;
        }

        ReadReleasedSoftware(info);

        // Send RPC response early — flash proceeds asynchronously from client's perspective
        if (OnResult) {
            OnResult(Json::Value("Ok"));
        }

        try {
            switch (SoftwareType) {
                case EFwSoftwareType::Firmware: {
                    DoFirmwareUpdate(*port, *traits, info);
                    break;
                }
                case EFwSoftwareType::Bootloader: {
                    DoBootloaderUpdate(*port, *traits, info);
                    break;
                }
                case EFwSoftwareType::Component: {
                    DoComponentsUpdate(*port, *traits, info);
                    break;
                }
            }
        } catch (const std::exception& e) {
            LOG(Error) << "Firmware update error: " << e.what();
            auto error = MakeFwUpdateStateError(e);
            State->SetError(SlaveId,
                            GetRPCPortDescription(PortSettings),
                            GetFwSoftwareTypeName(SoftwareType),
                            error.Id,
                            error.Message,
                            error.Metadata);
        }
        ReleaseLock();
    } catch (const TResponseTimeoutException& e) {
        ReleaseLock();
        if (OnError) {
            OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Device not responding: ") + e.what());
        }
    } catch (const std::exception& e) {
        ReleaseLock();
        if (OnError) {
            OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Error starting firmware update: ") + e.what());
        }
    }
    return ISerialClientTask::TRunResult::OK;
}

void TFwUpdateSerialClientTask::ReleaseLock()
{
    std::lock_guard<std::mutex> lock(UpdateLock->Mutex);
    UpdateLock->InProgress = false;
}

// Everything the flashing needs is taken from the release server before the request is answered,
// so a firmware missing there is reported as a failed request, as wb-device-manager did
void TFwUpdateSerialClientTask::ReadReleasedSoftware(const TFwDeviceInfo& info)
{
    if (SoftwareType == EFwSoftwareType::Bootloader) {
        ReleasedBootloader = Downloader->GetReleasedBootloader(info.FwSignature, ReleaseSuite);
    }
    if (SoftwareType != EFwSoftwareType::Component) {
        ReleasedFirmware = Downloader->GetReleasedFirmware(info.FwSignature, ReleaseSuite);
    }
    if (SoftwareType != EFwSoftwareType::Bootloader) {
        for (const auto& comp: info.Components) {
            ReleasedComponents[comp.Number] = Downloader->GetReleasedFirmware(comp.Signature, ReleaseSuite);
        }
    }
}

// Cf. firmware_update.py:785 FirmwareUpdater.update_software() — firmware branch
void TFwUpdateSerialClientTask::DoFirmwareUpdate(TPort& port, Modbus::IModbusTraits& traits, const TFwDeviceInfo& info)
{
    DoFlash(port,
            traits,
            EFwSoftwareType::Firmware,
            info.FwVersion,
            ReleasedFirmware.Version,
            ReleasedFirmware.Endpoint,
            true,
            info.CanPreservePortSettings);

    // Give the device time to start the new firmware before updating its components
    std::this_thread::sleep_for(std::chrono::seconds(1));
    // Also update components after firmware
    DoComponentsUpdate(port, traits, info);
}

// Cf. firmware_update.py:939 _update_bootloader()
void TFwUpdateSerialClientTask::DoBootloaderUpdate(TPort& port,
                                                   Modbus::IModbusTraits& traits,
                                                   const TFwDeviceInfo& info)
{
    DoFlash(port,
            traits,
            EFwSoftwareType::Bootloader,
            info.BootloaderVersion,
            ReleasedBootloader.Version,
            ReleasedBootloader.Endpoint,
            true,
            info.CanPreservePortSettings);

    // Auto-restore firmware after bootloader update (device stays in bootloader mode).
    // Give the device time to settle after rebooting into the bootloader before starting the flash.
    // Cf. firmware_update.py:973
    LOG(Info) << "Auto-restoring firmware after bootloader update for slave " << static_cast<int>(SlaveId);
    std::this_thread::sleep_for(std::chrono::seconds(1));

    DoFlash(port,
            traits,
            EFwSoftwareType::Firmware,
            "",
            ReleasedFirmware.Version,
            ReleasedFirmware.Endpoint,
            false, // already in bootloader
            false);
}

// Cf. firmware_update.py:538 update_components()
void TFwUpdateSerialClientTask::DoComponentsUpdate(TPort& port,
                                                   Modbus::IModbusTraits& traits,
                                                   const TFwDeviceInfo& info)
{
    for (const auto& comp: info.Components) {
        const auto& released = ReleasedComponents[comp.Number];
        try {
            if (ComponentFirmwareIsNewer(comp.FwVersion, released.Version)) {
                DoFlash(port,
                        traits,
                        EFwSoftwareType::Component,
                        comp.FwVersion,
                        released.Version,
                        released.Endpoint,
                        false, // components don't reboot to bootloader
                        false,
                        comp.Number,
                        comp.Model);
            }
        } catch (const std::exception& e) {
            // The other components are updated all the same, so the failure is reported here
            // and not by the caller
            LOG(Error) << "Cannot update component " << comp.Number << ": " << e.what();
            auto error = MakeFwUpdateStateError(e);
            State->SetError(SlaveId,
                            GetRPCPortDescription(PortSettings),
                            GetFwSoftwareTypeName(EFwSoftwareType::Component),
                            error.Id,
                            error.Message,
                            error.Metadata);
        }
    }
}

void TFwUpdateSerialClientTask::DoFlash(TPort& port,
                                        Modbus::IModbusTraits& traits,
                                        EFwSoftwareType type,
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
    updateInfo.PortPath = GetRPCPortDescription(PortSettings);
    updateInfo.Protocol = Protocol;
    updateInfo.SlaveId = SlaveId;
    updateInfo.ToVersion = toVersion;
    updateInfo.Progress = 0;
    updateInfo.FromVersion = fromVersion;
    updateInfo.Type = GetFwSoftwareTypeName(type);
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
    State->Remove(SlaveId, GetRPCPortDescription(PortSettings), GetFwSoftwareTypeName(type));
}
