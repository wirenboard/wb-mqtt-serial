#include "rpc_fw_update_handler.h"
#include "rpc_fw_update_helpers.h"
#include "common_utils.h"
#include "log.h"

#include <curl/curl.h>
#include <thread>

#define LOG(logger) ::logger.Log() << "[fw-update] "

// Original Python implementation: https://github.com/wirenboard/wb-device-manager/blob/main/wb/device_manager/firmware_update.py
// See also: FirmwareUpdater class (line 650)

namespace
{
    const std::string STATE_TOPIC = "/wb-device-manager/firmware_update/state";
}

// Non-updatable signatures (e.g. WB-MSW-LORA devices)
bool IsNonUpdatableSignature(const std::string& sig)
{
    return sig == "msw5GL" || sig == "msw3G419L";
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
    std::call_once(curlInitFlag, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });

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

    if (!request.isMember("port") || !request["port"].isMember("path") ||
        request["port"]["path"].asString().empty())
    {
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

// Cf. firmware_update.py:682 FirmwareUpdater.get_firmware_info() - response building part
Json::Value TRPCFwUpdateHandler::BuildFirmwareInfoResponse(const TFwDeviceInfo& deviceInfo)
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
        auto released = Downloader->GetReleasedFirmware(deviceInfo.FwSignature, ReleaseSuite);
        result["available_fw"] = released.Version;
        result["fw_has_update"] = FirmwareIsNewer(deviceInfo.FwVersion, released.Version);
    } catch (const std::exception& e) {
        LOG(Warn) << "Cannot get released firmware for " << deviceInfo.FwSignature << ": " << e.what();
    }

    // Look up bootloader
    try {
        auto bootloader = Downloader->GetLatestBootloader(deviceInfo.FwSignature);
        result["available_bootloader"] = bootloader.Version;
        result["bootloader_has_update"] = FirmwareIsNewer(deviceInfo.BootloaderVersion, bootloader.Version);
    } catch (const std::exception& e) {
        LOG(Debug) << "Cannot get bootloader info for " << deviceInfo.FwSignature << ": " << e.what();
    }

    // Check if device can be updated (serial always true, TCP depends on port settings preservation)
    result["can_update"] = true;

    // Component info
    for (const auto& comp: deviceInfo.Components) {
        try {
            auto released = Downloader->GetReleasedFirmware(comp.Signature, ReleaseSuite);
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

        // Create a task to read device registers
        auto downloader = Downloader;
        auto state = State;
        auto releaseSuite = ReleaseSuite;
        auto handler = this;

        auto task = std::make_shared<TFwGetInfoTask>(
            static_cast<uint8_t>(params.SlaveId),
            params.Protocol,
            [handler, onResult, onError](const TFwDeviceInfo& info) {
                try {
                    auto result = handler->BuildFirmwareInfoResponse(info);
                    onResult(result);
                } catch (const std::exception& e) {
                    onError(WBMQTT::E_RPC_SERVER_ERROR, std::string("Error building firmware info: ") + e.what());
                }
            },
            [onError](const std::string& error) { onError(WBMQTT::E_RPC_SERVER_ERROR, error); });

        auto portRequest = MakePortRequestJson(params);
        SerialClientTaskRunner.RunTask(portRequest, task);
    } catch (const std::exception& e) {
        onError(WBMQTT::E_RPC_SERVER_ERROR, e.what());
    }
}

// Cf. firmware_update.py:444 update_software() and firmware_update.py:633 make_device_update_info()
void TRPCFwUpdateHandler::StartFlash(const TRequestParams& params,
                                      const std::string& type,
                                      const std::string& fromVersion,
                                      const std::string& toVersion,
                                      const std::string& fwUrl,
                                      bool rebootToBootloader,
                                      bool canPreservePortSettings,
                                      int componentNumber,
                                      const std::string& componentModel,
                                      std::function<void()> customCompleteCallback)
{
    // Update state to show 0% progress
    TDeviceUpdateInfo info;
    info.PortPath = params.PortPath;
    info.Protocol = params.Protocol;
    info.SlaveId = params.SlaveId;
    info.ToVersion = toVersion;
    info.Progress = 0;
    info.FromVersion = fromVersion;
    info.Type = type;
    info.ComponentNumber = componentNumber;
    info.ComponentModel = componentModel;
    State->Update(info);

    // Download and parse firmware file
    auto firmware = Downloader->DownloadAndParseWBFW(fwUrl);

    // Create update notifier for throttled progress publishing
    auto notifier = std::make_shared<TUpdateNotifier>(30);
    auto state = State;
    auto portPath = params.PortPath;
    auto protocol = params.Protocol;
    auto slaveId = params.SlaveId;

    auto flashTask = std::make_shared<TFwFlashTask>(
        static_cast<uint8_t>(params.SlaveId),
        params.Protocol,
        std::move(firmware),
        rebootToBootloader,
        canPreservePortSettings,
        // Progress callback
        [state, notifier, portPath, protocol, slaveId, toVersion, fromVersion, type, componentNumber, componentModel](
            int percent) {
            if (notifier->ShouldNotify(percent)) {
                TDeviceUpdateInfo progressInfo;
                progressInfo.PortPath = portPath;
                progressInfo.Protocol = protocol;
                progressInfo.SlaveId = slaveId;
                progressInfo.ToVersion = toVersion;
                progressInfo.Progress = percent;
                progressInfo.FromVersion = fromVersion;
                progressInfo.Type = type;
                progressInfo.ComponentNumber = componentNumber;
                progressInfo.ComponentModel = componentModel;
                state->Update(progressInfo);
            }
        },
        // Complete callback
        customCompleteCallback
            ? [customCompleteCallback]() { customCompleteCallback(); }
            : std::function<void()>([state, slaveId, portPath, type]() { state->Remove(slaveId, portPath, type); }),
        // Error callback
        [state, slaveId, portPath, type](const std::string& error) {
            std::string errorId = "com.wb.device_manager.generic_error";
            std::string errorMsg = "Internal error. Check logs for more info";
            Json::Value metadata;
            metadata["exception"] = error;
            state->SetError(slaveId, portPath, type, errorId, errorMsg, metadata);
        });

    auto portRequest = MakePortRequestJson(params);
    SerialClientTaskRunner.RunTask(portRequest, flashTask);
}

// Cf. firmware_update.py:538 update_components()
void TRPCFwUpdateHandler::StartComponentsFlash(const TRequestParams& params,
                                                const TFwDeviceInfo& deviceInfo,
                                                const std::string& releaseSuite)
{
    for (const auto& comp: deviceInfo.Components) {
        try {
            auto released = Downloader->GetReleasedFirmware(comp.Signature, releaseSuite);
            if (ComponentFirmwareIsNewer(comp.FwVersion, released.Version)) {
                StartFlash(params,
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

// Cf. firmware_update.py:785 FirmwareUpdater.update_software()
void TRPCFwUpdateHandler::Update(const Json::Value& request,
                                  WBMQTT::TMqttRpcServer::TResultCallback onResult,
                                  WBMQTT::TMqttRpcServer::TErrorCallback onError)
{
    try {
        {
            std::lock_guard<std::mutex> lock(UpdateMutex);
            if (UpdateInProgress) {
                onError(WBMQTT::E_RPC_SERVER_ERROR, "Task is already executing.");
                return;
            }
            UpdateInProgress = true;
        }

        auto params = ParseRequestParams(request);
        auto softwareType = request.get("type", "firmware").asString();

        // Read device info first
        auto downloader = Downloader;
        auto state = State;
        auto releaseSuite = ReleaseSuite;
        auto handler = this;
        auto portRequest = MakePortRequestJson(params);

        auto getInfoTask = std::make_shared<TFwGetInfoTask>(
            static_cast<uint8_t>(params.SlaveId),
            params.Protocol,
            [handler, params, softwareType, onResult, onError, releaseSuite](const TFwDeviceInfo& info) {
                try {
                    if (softwareType == "firmware") {
                        auto released =
                            handler->Downloader->GetReleasedFirmware(info.FwSignature, releaseSuite);
                        handler->StartFlash(params,
                                            "firmware",
                                            info.FwVersion,
                                            released.Version,
                                            released.Endpoint,
                                            true,
                                            info.CanPreservePortSettings);

                        // Also schedule component updates after firmware
                        handler->StartComponentsFlash(params, info, releaseSuite);
                    } else if (softwareType == "bootloader") {
                        auto bootloader = handler->Downloader->GetLatestBootloader(info.FwSignature);
                        auto fwSignature = info.FwSignature;
                        // Cf. firmware_update.py:939 _update_bootloader():
                        // After BL flash, auto-restore firmware (device stays in BL mode)
                        auto autoRestoreCallback = [handler, params, fwSignature, releaseSuite]() {
                            try {
                                // Remove bootloader state entry
                                handler->State->Remove(params.SlaveId, params.PortPath, "bootloader");
                                // Look up released firmware for this device
                                auto released = handler->Downloader->GetReleasedFirmware(fwSignature, releaseSuite);
                                LOG(Info) << "Auto-restoring firmware after bootloader update for slave "
                                          << params.SlaveId;
                                // Wait for device to stabilize after bootloader flash
                                // Cf. firmware_update.py:973 await asyncio.sleep(1)
                                std::this_thread::sleep_for(std::chrono::seconds(2));
                                // Flash firmware (device is already in bootloader mode)
                                handler->StartFlash(params,
                                                    "firmware",
                                                    "",
                                                    released.Version,
                                                    released.Endpoint,
                                                    false,  // already in bootloader
                                                    false);
                            } catch (const std::exception& e) {
                                LOG(Error) << "Failed to auto-restore firmware after bootloader update: " << e.what();
                                handler->State->SetError(params.SlaveId,
                                                         params.PortPath,
                                                         "firmware",
                                                         "com.wb.device_manager.generic_error",
                                                         "Failed to restore firmware after bootloader update",
                                                         Json::Value());
                            }
                        };
                        handler->StartFlash(params,
                                            "bootloader",
                                            info.BootloaderVersion,
                                            bootloader.Version,
                                            bootloader.Endpoint,
                                            true,
                                            info.CanPreservePortSettings,
                                            -1,
                                            "",
                                            autoRestoreCallback);
                    } else if (softwareType == "component") {
                        handler->StartComponentsFlash(params, info, releaseSuite);
                    }

                    Json::Value result;
                    result = "Ok";
                    onResult(result);

                    // Mark update as no longer in progress (allows next update)
                    std::lock_guard<std::mutex> lock(handler->UpdateMutex);
                    handler->UpdateInProgress = false;
                } catch (const std::exception& e) {
                    {
                        std::lock_guard<std::mutex> lock(handler->UpdateMutex);
                        handler->UpdateInProgress = false;
                    }
                    onError(WBMQTT::E_RPC_SERVER_ERROR,
                            std::string("Error starting firmware update: ") + e.what());
                }
            },
            [handler, onError](const std::string& error) {
                {
                    std::lock_guard<std::mutex> lock(handler->UpdateMutex);
                    handler->UpdateInProgress = false;
                }
                onError(WBMQTT::E_RPC_SERVER_ERROR, error);
            });

        SerialClientTaskRunner.RunTask(portRequest, getInfoTask);
    } catch (const std::exception& e) {
        {
            std::lock_guard<std::mutex> lock(UpdateMutex);
            UpdateInProgress = false;
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
            std::lock_guard<std::mutex> lock(UpdateMutex);
            if (UpdateInProgress) {
                onError(WBMQTT::E_RPC_SERVER_ERROR, "Task is already executing.");
                return;
            }
            UpdateInProgress = true;
        }

        auto params = ParseRequestParams(request);

        // Read firmware signature from device in bootloader mode
        auto handler = this;
        auto releaseSuite = ReleaseSuite;
        auto portRequest = MakePortRequestJson(params);

        auto getInfoTask = std::make_shared<TFwGetInfoTask>(
            static_cast<uint8_t>(params.SlaveId),
            params.Protocol,
            [handler, params, onResult, onError, releaseSuite](const TFwDeviceInfo& info) {
                try {
                    auto released =
                        handler->Downloader->GetReleasedFirmware(info.FwSignature, releaseSuite);
                    handler->StartFlash(params,
                                        "firmware",
                                        "",
                                        released.Version,
                                        released.Endpoint,
                                        false, // already in bootloader
                                        false);

                    Json::Value result;
                    result = "Ok";
                    onResult(result);

                    std::lock_guard<std::mutex> lock(handler->UpdateMutex);
                    handler->UpdateInProgress = false;
                } catch (const std::exception& e) {
                    {
                        std::lock_guard<std::mutex> lock(handler->UpdateMutex);
                        handler->UpdateInProgress = false;
                    }
                    onError(WBMQTT::E_RPC_SERVER_ERROR,
                            std::string("Error starting firmware restore: ") + e.what());
                }
            },
            [handler, onResult, onError](const std::string& error) {
                // If device is not in bootloader mode, return Ok silently
                {
                    std::lock_guard<std::mutex> lock(handler->UpdateMutex);
                    handler->UpdateInProgress = false;
                }
                Json::Value result;
                result = "Ok";
                onResult(result);
            });

        SerialClientTaskRunner.RunTask(portRequest, getInfoTask);
    } catch (const std::exception& e) {
        {
            std::lock_guard<std::mutex> lock(UpdateMutex);
            UpdateInProgress = false;
        }
        onError(WBMQTT::E_RPC_SERVER_ERROR, e.what());
    }
}
