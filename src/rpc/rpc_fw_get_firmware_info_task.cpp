#include "rpc_fw_get_firmware_info_task.h"
#include "port/port.h"
#include "rpc_fw_update_helpers.h"
#include "rpc_helpers.h"
#include "serial_exc.h"

TFwGetFirmwareInfoTask::TFwGetFirmwareInfoTask(uint8_t slaveId,
                                               const std::string& protocol,
                                               const std::string& releaseSuite,
                                               const TPortSettings& portSettings,
                                               PSerialDevice deviceFromConfig,
                                               std::shared_ptr<TFwDownloader> downloader,
                                               WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                               WBMQTT::TMqttRpcServer::TErrorCallback onError)
    : SlaveId(slaveId),
      Protocol(protocol),
      ReleaseSuite(releaseSuite),
      PortSettings(portSettings),
      DeviceFromConfig(std::move(deviceFromConfig)),
      Downloader(std::move(downloader)),
      OnResult(std::move(onResult)),
      OnError(std::move(onError))
{}

ISerialClientTask::TRunResult TFwGetFirmwareInfoTask::Run(PFeaturePort port,
                                                          TSerialClientDeviceAccessHandler& lastAccessedDevice,
                                                          const std::list<PSerialDevice>& polledDevices)
{
    if (IsCancelled()) {
        AnswerRequestCancelled(OnError);
        return ISerialClientTask::TRunResult::OK;
    }
    try {
        if (!port->IsOpen()) {
            port->Open();
        }
        lastAccessedDevice.PrepareToAccess(*port, nullptr);
        Json::Value result;
        if (DeviceFromConfig) {
            result = ReadFirmwareInfo(port);
        } else {
            TSerialPortSettingsGuard settingsGuard(port, GetRPCPortConnectionSettings(PortSettings));
            result = ReadFirmwareInfo(port);
        }
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

Json::Value TFwGetFirmwareInfoTask::ReadFirmwareInfo(PFeaturePort port)
{
    port->SkipNoise();
    auto traits = MakeModbusTraits(Protocol);
    auto info = ReadFwDeviceInfo(*traits, *port, SlaveId);
    auto updatable = true;
    auto tcpPort = DeviceFromConfig ? std::dynamic_pointer_cast<TTcpPort>(port->GetBasePort()) != nullptr
                                    : std::holds_alternative<TFramedTcpPortSettings>(PortSettings);
    if (RequiresDefaultPortSettings(tcpPort, Protocol, info.CanPreservePortSettings)) {
        updatable = HasDefaultPortSettings(*traits, *port, SlaveId);
    }
    return BuildFirmwareInfoResponse(info, *Downloader, ReleaseSuite, updatable, ENetworkAccess::CacheOnly);
}
