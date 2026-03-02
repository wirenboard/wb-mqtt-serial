#include "rpc_fw_restore_task.h"
#include "log.h"
#include "modbus_base.h"
#include "serial_exc.h"

#define LOG(logger) ::logger.Log() << "[fw-update] "

TFwRestoreTask::TFwRestoreTask(uint8_t slaveId,
                               const std::string& protocol,
                               const std::string& portPath,
                               const std::string& releaseSuite,
                               std::shared_ptr<TFwDownloader> downloader,
                               PFwUpdateState state,
                               std::mutex& updateMutex,
                               bool& updateInProgress,
                               WBMQTT::TMqttRpcServer::TResultCallback onResult,
                               WBMQTT::TMqttRpcServer::TErrorCallback onError)
    : SlaveId(slaveId),
      Protocol(protocol),
      PortPath(portPath),
      ReleaseSuite(releaseSuite),
      Downloader(std::move(downloader)),
      State(std::move(state)),
      UpdateMutex(updateMutex),
      UpdateInProgress(updateInProgress),
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
        port->SkipNoise();

        auto traits = MakeModbusTraits(Protocol);

        // Try to read device info. If device is not in bootloader mode, it will fail.
        TFwDeviceInfo info;
        try {
            info = ReadFwDeviceInfo(*port, *traits, SlaveId);
        } catch (const std::exception&) {
            // Device not responding — return "Ok" silently (current behavior)
            {
                std::lock_guard<std::mutex> lock(UpdateMutex);
                UpdateInProgress = false;
            }
            if (OnResult) {
                Json::Value result;
                result = "Ok";
                OnResult(result);
            }
            return ISerialClientTask::TRunResult::OK;
        }

        // Device responded — proceed with firmware restore
        auto released = Downloader->GetReleasedFirmware(info.FwSignature, ReleaseSuite);

        // Send RPC response early
        if (OnResult) {
            Json::Value result;
            result = "Ok";
            OnResult(result);
        }
        {
            std::lock_guard<std::mutex> lock(UpdateMutex);
            UpdateInProgress = false;
        }

        // Update state and flash
        TDeviceUpdateInfo updateInfo;
        updateInfo.PortPath = PortPath;
        updateInfo.Protocol = Protocol;
        updateInfo.SlaveId = SlaveId;
        updateInfo.ToVersion = released.Version;
        updateInfo.Progress = 0;
        updateInfo.FromVersion = "";
        updateInfo.Type = "firmware";
        State->Update(updateInfo);

        auto firmware = Downloader->DownloadAndParseWBFW(released.Endpoint);

        TUpdateNotifier notifier(30);
        FlashFirmware(*port, *traits, SlaveId, firmware, false, false, [&](int percent) {
            if (notifier.ShouldNotify(percent)) {
                updateInfo.Progress = percent;
                State->Update(updateInfo);
            }
        });

        State->Remove(SlaveId, PortPath, "firmware");
    } catch (const TResponseTimeoutException& e) {
        {
            std::lock_guard<std::mutex> lock(UpdateMutex);
            UpdateInProgress = false;
        }
        if (OnError) {
            OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Device not responding: ") + e.what());
        }
    } catch (const std::exception& e) {
        {
            std::lock_guard<std::mutex> lock(UpdateMutex);
            UpdateInProgress = false;
        }
        if (OnError) {
            OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Error starting firmware restore: ") + e.what());
        }
    }
    return ISerialClientTask::TRunResult::OK;
}
