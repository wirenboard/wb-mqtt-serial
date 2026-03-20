#include "rpc_fw_get_firmware_info_task.h"
#include "port/port.h"
#include "rpc_fw_update_helpers.h"
#include "rpc_helpers.h"
#include "serial_exc.h"

TFwGetFirmwareInfoTask::TFwGetFirmwareInfoTask(uint8_t slaveId,
                                               const std::string& protocol,
                                               const std::string& releaseSuite,
                                               const TSerialPortConnectionSettings& portSettings,
                                               std::shared_ptr<TFwDownloader> downloader,
                                               WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                               WBMQTT::TMqttRpcServer::TErrorCallback onError)
    : SlaveId(slaveId),
      Protocol(protocol),
      ReleaseSuite(releaseSuite),
      PortSettings(portSettings),
      Downloader(std::move(downloader)),
      OnResult(std::move(onResult)),
      OnError(std::move(onError))
{}

ISerialClientTask::TRunResult TFwGetFirmwareInfoTask::Run(PFeaturePort port,
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
        auto result = BuildFirmwareInfoResponse(info, *Downloader, ReleaseSuite);

        if (OnResult) {
            OnResult(result);
        }
    } catch (const TResponseTimeoutException& e) {
        if (OnError) {
            OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Device not responding: ") + e.what());
        }
    } catch (const std::exception& e) {
        if (OnError) {
            OnError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Error: ") + e.what());
        }
    }
    return ISerialClientTask::TRunResult::OK;
}
