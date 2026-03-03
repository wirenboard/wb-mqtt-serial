#include "rpc_fw_get_firmware_info_task.h"
#include "modbus_base.h"
#include "rpc_fw_update_helpers.h"
#include "serial_exc.h"

TFwGetFirmwareInfoTask::TFwGetFirmwareInfoTask(uint8_t slaveId,
                                               const std::string& protocol,
                                               std::shared_ptr<TFwDownloader> downloader,
                                               const std::string& releaseSuite,
                                               WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                               WBMQTT::TMqttRpcServer::TErrorCallback onError)
    : SlaveId(slaveId),
      Protocol(protocol),
      Downloader(std::move(downloader)),
      ReleaseSuite(releaseSuite),
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
        port->SkipNoise();

        auto traits = MakeModbusTraits(Protocol);
        auto info = ReadFwDeviceInfo(*port, *traits, SlaveId);
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
