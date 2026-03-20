#include "rpc_fw_update_handler.h"
#include "common_utils.h"
#include "log.h"
#include "rpc_fw_get_firmware_info_task.h"
#include "rpc_fw_restore_task.h"
#include "rpc_fw_update_helpers.h"
#include "rpc_fw_update_serial_client_task.h"

#include <cstdlib>
#include <curl/curl.h>

#define LOG(logger) ::logger.Log() << "[fw-update] "

// Original Python implementation:
// https://github.com/wirenboard/wb-device-manager/blob/main/wb/device_manager/firmware_update.py See also:
// FirmwareUpdater class (line 650)

namespace
{
    const std::string STATE_TOPIC = "/wb-mqtt-serial/firmware_update/state";
}

bool IsNonUpdatableSignature(const std::string& sig)
{
    return std::find(NonUpdatableSignatures.begin(), NonUpdatableSignatures.end(), sig) !=
           NonUpdatableSignatures.end();
}

// Cf. version_comparison.py:8 firmware_is_newer()
bool FirmwareIsNewer(const std::string& currentVersion, const std::string& availableVersion)
{
    if (currentVersion.empty() || availableVersion.empty()) {
        return false;
    }
    return util::CompareVersionStrings(availableVersion, currentVersion) > 0;
}

// Cf. version_comparison.py:4 component_firmware_is_newer()
bool ComponentFirmwareIsNewer(const std::string& currentVersion, const std::string& availableVersion)
{
    return !availableVersion.empty() && currentVersion != availableVersion;
}

// Cf. firmware_update.py:682 FirmwareUpdater.get_firmware_info() - response building part
Json::Value BuildFirmwareInfoResponse(const TFwDeviceInfo& deviceInfo,
                                      TFwDownloader& downloader,
                                      const std::string& releaseSuite)
{
    Json::Value result;
    result["fw"] = deviceInfo.FwVersion;
    result["available_fw"] = "";
    result["can_update"] = false;
    result["fw_has_update"] = false;
    result["bootloader"] = deviceInfo.BootloaderVersion;
    result["available_bootloader"] = "";
    result["bootloader_has_update"] = false;
    result["components"] = Json::Value(Json::objectValue);
    result["model"] = deviceInfo.DeviceModel;

    // Skip non-updatable signatures
    if (IsNonUpdatableSignature(deviceInfo.FwSignature)) {
        return result;
    }

    // Look up released firmware
    try {
        auto released = downloader.GetReleasedFirmware(deviceInfo.FwSignature, releaseSuite);
        result["available_fw"] = released.Version;
        result["fw_has_update"] = FirmwareIsNewer(deviceInfo.FwVersion, released.Version);
    } catch (const std::exception& e) {
        LOG(Warn) << "Cannot get released firmware for " << deviceInfo.FwSignature << ": " << e.what();
    }

    // Look up bootloader
    try {
        auto bootloader = downloader.GetLatestBootloader(deviceInfo.FwSignature);
        result["available_bootloader"] = bootloader.Version;
        result["bootloader_has_update"] = FirmwareIsNewer(deviceInfo.BootloaderVersion, bootloader.Version);
    } catch (const std::exception& e) {
        LOG(Debug) << "Cannot get bootloader info for " << deviceInfo.FwSignature << ": " << e.what();
    }

    // Serial devices are always updatable. TCP devices would need additional checks
    // (port settings preservation, protocol type, baud rate), but wb-mqtt-serial only handles serial.
    result["can_update"] = true;

    // Component info
    for (const auto& comp: deviceInfo.Components) {
        try {
            auto released = downloader.GetReleasedFirmware(comp.Signature, releaseSuite);
            Json::Value compJson;
            compJson["model"] = comp.Model;
            compJson["fw"] = comp.FwVersion;
            compJson["available_fw"] = released.Version;
            compJson["has_update"] = ComponentFirmwareIsNewer(comp.FwVersion, released.Version);
            result["components"][std::to_string(comp.Number)] = compJson;
        } catch (const std::exception& e) {
            LOG(Debug) << "Cannot get component " << comp.Number << " firmware info: " << e.what();
        }
    }

    return result;
}

// Cf. firmware_update.py:650 FirmwareUpdater.__init__()
TRPCFwUpdateHandler::TRPCFwUpdateHandler(ITaskRunner& serialClientTaskRunner,
                                         WBMQTT::PMqttRpcServer rpcServer,
                                         WBMQTT::PMqttClient mqtt,
                                         PHttpClient httpClient)
    : SerialClientTaskRunner(serialClientTaskRunner),
      Mqtt(mqtt)
{
    // Initialize libcurl globally (thread-safe, must be called before any curl_easy_init)
    static std::once_flag curlInitFlag;
    std::call_once(curlInitFlag, []() {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        std::atexit(curl_global_cleanup);
    });

    if (!httpClient) {
        httpClient = std::make_shared<TCurlHttpClient>();
    }
    Downloader = std::make_shared<TFwDownloader>(httpClient);

    State = std::make_shared<TFwUpdateState>(
        [this](const std::string& topic, const std::string& payload, bool retain) {
            if (Mqtt) {
                Mqtt->Publish(WBMQTT::TMqttMessage(topic, payload, 0, retain));
            }
        },
        STATE_TOPIC);

    ReleaseSuite = ReadReleaseSuite();

    // Publish initial empty state
    State->Reset();

    RegisterRpcMethods(rpcServer);
    rpcServer->Start();
}

TRPCFwUpdateHandler::TRPCFwUpdateHandler(ITaskRunner& taskRunner,
                                         PHttpClient httpClient,
                                         PFwUpdateState state,
                                         const std::string& releaseSuite)
    : SerialClientTaskRunner(taskRunner),
      Downloader(std::make_shared<TFwDownloader>(httpClient)),
      State(state),
      ReleaseSuite(releaseSuite)
{}

void TRPCFwUpdateHandler::RegisterRpcMethods(WBMQTT::PMqttRpcServer rpcServer)
{
    rpcServer->RegisterAsyncMethod("fw-update",
                                   "GetFirmwareInfo",
                                   std::bind(&TRPCFwUpdateHandler::GetFirmwareInfo,
                                             this,
                                             std::placeholders::_1,
                                             std::placeholders::_2,
                                             std::placeholders::_3));
    rpcServer->RegisterAsyncMethod("fw-update",
                                   "Update",
                                   std::bind(&TRPCFwUpdateHandler::Update,
                                             this,
                                             std::placeholders::_1,
                                             std::placeholders::_2,
                                             std::placeholders::_3));
    rpcServer->RegisterMethod("fw-update",
                              "ClearError",
                              std::bind(&TRPCFwUpdateHandler::ClearError, this, std::placeholders::_1));
    rpcServer->RegisterAsyncMethod("fw-update",
                                   "Restore",
                                   std::bind(&TRPCFwUpdateHandler::Restore,
                                             this,
                                             std::placeholders::_1,
                                             std::placeholders::_2,
                                             std::placeholders::_3));
}

// Cf. serial_device.py:135 create_device_from_json()
TRPCFwUpdateHandler::TRequestParams TRPCFwUpdateHandler::ParseRequestParams(const Json::Value& request)
{
    TRequestParams params;

    if (!request.isMember("slave_id") || !request["slave_id"].isInt()) {
        throw std::runtime_error("slave_id is required and must be an integer");
    }
    params.SlaveId = request["slave_id"].asInt();
    if (params.SlaveId < 0 || params.SlaveId > 255) {
        throw std::runtime_error("slave_id must be in range 0-255");
    }

    if (!request.isMember("port") || !request["port"].isMember("path") || request["port"]["path"].asString().empty()) {
        throw std::runtime_error("port.path is required");
    }
    params.PortPath = request["port"]["path"].asString();

    params.Protocol = request.get("protocol", "modbus").asString();

    return params;
}

Json::Value TRPCFwUpdateHandler::MakePortRequestJson(const TRequestParams& params)
{
    Json::Value req;
    req["path"] = params.PortPath;
    req["slave_id"] = std::to_string(params.SlaveId);
    req["protocol"] = params.Protocol;
    return req;
}

// Cf. firmware_update.py:682 FirmwareUpdater.get_firmware_info()
void TRPCFwUpdateHandler::GetFirmwareInfo(const Json::Value& request,
                                          WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                          WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    try {
        auto params = ParseRequestParams(request);

        if (State->HasActiveUpdate(params.SlaveId, params.PortPath)) {
            onError(WBMQTT::E_RPC_SERVER_ERROR, "Task is already executing.");
            return;
        }

        auto task = std::make_shared<TFwGetFirmwareInfoTask>(static_cast<uint8_t>(params.SlaveId),
                                                             params.Protocol,
                                                             Downloader,
                                                             ReleaseSuite,
                                                             std::move(onResult),
                                                             std::move(onError));
        SerialClientTaskRunner.RunTask(MakePortRequestJson(params), task);
    } catch (const std::exception& e) {
        onError(WBMQTT::E_RPC_SERVER_ERROR, e.what());
    }
}

// Cf. firmware_update.py:785 FirmwareUpdater.update_software()
void TRPCFwUpdateHandler::Update(const Json::Value& request,
                                 WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                 WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    try {
        {
            std::lock_guard<std::mutex> lock(UpdateLock->Mutex);
            if (UpdateLock->InProgress) {
                onError(WBMQTT::E_RPC_SERVER_ERROR, "Task is already executing.");
                return;
            }
            UpdateLock->InProgress = true;
        }

        auto params = ParseRequestParams(request);
        auto softwareType = request.get("type", "firmware").asString();

        auto task = std::make_shared<TFwUpdateSerialClientTask>(static_cast<uint8_t>(params.SlaveId),
                                                                params.Protocol,
                                                                softwareType,
                                                                params.PortPath,
                                                                ReleaseSuite,
                                                                Downloader,
                                                                State,
                                                                UpdateLock,
                                                                std::move(onResult),
                                                                std::move(onError));
        SerialClientTaskRunner.RunTask(MakePortRequestJson(params), task);
    } catch (const std::exception& e) {
        {
            std::lock_guard<std::mutex> lock(UpdateLock->Mutex);
            UpdateLock->InProgress = false;
        }
        onError(WBMQTT::E_RPC_SERVER_ERROR, e.what());
    }
}

// Cf. firmware_update.py:850 FirmwareUpdater.clear_error()
Json::Value TRPCFwUpdateHandler::ClearError(const Json::Value& request)
{
    auto params = ParseRequestParams(request);
    auto softwareType = request.get("type", "firmware").asString();
    State->ClearError(params.SlaveId, params.PortPath, softwareType);
    return Json::Value("Ok");
}

// Cf. firmware_update.py:871 FirmwareUpdater.restore_firmware() and firmware_update.py:1015 _restore_firmware()
void TRPCFwUpdateHandler::Restore(const Json::Value& request,
                                  WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                  WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    try {
        {
            std::lock_guard<std::mutex> lock(UpdateLock->Mutex);
            if (UpdateLock->InProgress) {
                onError(WBMQTT::E_RPC_SERVER_ERROR, "Task is already executing.");
                return;
            }
            UpdateLock->InProgress = true;
        }

        auto params = ParseRequestParams(request);

        auto task = std::make_shared<TFwRestoreTask>(static_cast<uint8_t>(params.SlaveId),
                                                     params.Protocol,
                                                     params.PortPath,
                                                     ReleaseSuite,
                                                     Downloader,
                                                     State,
                                                     UpdateLock,
                                                     std::move(onResult),
                                                     std::move(onError));
        SerialClientTaskRunner.RunTask(MakePortRequestJson(params), task);
    } catch (const std::exception& e) {
        {
            std::lock_guard<std::mutex> lock(UpdateLock->Mutex);
            UpdateLock->InProgress = false;
        }
        onError(WBMQTT::E_RPC_SERVER_ERROR, e.what());
    }
}
