#include "rpc_fw_restore_task.h"
#include "log.h"
#include "port/port.h"
#include "rpc_helpers.h"
#include "serial_exc.h"

#define LOG(logger) ::logger.Log() << "[fw-update] "

TFwRestoreTask::TFwRestoreTask(uint8_t slaveId,
                               const std::string& protocol,
                               const std::string& releaseSuite,
                               const TRPCPortSettings& portSettings,
                               std::shared_ptr<TFwDownloader> downloader,
                               PFwUpdateState state,
                               PFwUpdateLock updateLock,
                               WBMQTT::TMqttRpcServer::TResultCallback onResult,
                               WBMQTT::TMqttRpcServer::TErrorCallback onError)
    : SlaveId(slaveId),
      Protocol(protocol),
      ReleaseSuite(releaseSuite),
      PortSettings(portSettings),
      Downloader(std::move(downloader)),
      State(std::move(state)),
      UpdateLock(std::move(updateLock)),
      OnResult(std::move(onResult)),
      OnError(std::move(onError))
{}

// Cf. firmware_update.py:871 FirmwareUpdater.restore_firmware() and firmware_update.py:1015 _restore_firmware()
ISerialClientTask::TRunResult TFwRestoreTask::Run(PFeaturePort port,
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

        if (!IsInBootloaderMode(*traits, *port, SlaveId)) {
            ReleaseLock();
            if (OnResult) {
                OnResult(Json::Value("Ok"));
            }
            return ISerialClientTask::TRunResult::OK;
        }

        auto info = ReadFwDeviceInfo(*traits, *port, SlaveId);

        auto released = Downloader->GetReleasedFirmware(info.FwSignature, ReleaseSuite);

        // Send RPC response early
        if (OnResult) {
            OnResult(Json::Value("Ok"));
        }

        auto portDescription = GetRPCPortDescription(PortSettings);
        auto softwareTypeName = GetFwSoftwareTypeName(EFwSoftwareType::Firmware);
        try {
            // Update state and flash
            TDeviceUpdateInfo updateInfo;
            updateInfo.PortPath = portDescription;
            updateInfo.Protocol = Protocol;
            updateInfo.SlaveId = SlaveId;
            updateInfo.ToVersion = released.Version;
            updateInfo.Progress = 0;
            updateInfo.Type = softwareTypeName;
            State->Update(updateInfo);

            auto firmware = Downloader->DownloadAndParseWBFW(released.Endpoint);

            TUpdateNotifier notifier(30);
            FlashFirmware(*traits, *port, SlaveId, firmware, false, false, [&](int percent) {
                if (notifier.ShouldNotify(percent)) {
                    updateInfo.Progress = percent;
                    State->Update(updateInfo);
                }
            });

            State->Remove(SlaveId, portDescription, softwareTypeName);
        } catch (const std::exception& e) {
            LOG(Error) << "Firmware restore error: " << e.what();
            auto error = MakeFwUpdateStateError(e);
            State->SetError(SlaveId, portDescription, softwareTypeName, error.Id, error.Message, error.Metadata);
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
            OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Error starting firmware restore: ") + e.what());
        }
    }
    return ISerialClientTask::TRunResult::OK;
}

void TFwRestoreTask::ReleaseLock()
{
    std::lock_guard<std::mutex> lock(UpdateLock->Mutex);
    UpdateLock->InProgress = false;
}
