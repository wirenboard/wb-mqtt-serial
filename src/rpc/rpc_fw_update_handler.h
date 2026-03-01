#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include <wblib/rpc.h>
#include <wblib/wbmqtt.h>

#include "rpc_fw_downloader.h"
#include "rpc_fw_update_state.h"
#include "rpc_fw_update_task.h"
#include "rpc_port_driver_list.h"

class TRPCFwUpdateHandler
{
    friend class FwHandlerTest;
    friend class FwHandlerIntegrationTest;

public:
    TRPCFwUpdateHandler(ITaskRunner& serialClientTaskRunner,
                        WBMQTT::PMqttRpcServer rpcServer,
                        WBMQTT::PMqttClient mqtt,
                        PHttpClient httpClient = nullptr);

private:
    // Test constructor: initializes without MQTT/RPC infrastructure
    TRPCFwUpdateHandler(ITaskRunner& taskRunner,
                        PHttpClient httpClient,
                        PFwUpdateState state,
                        const std::string& releaseSuite);
    void RegisterRpcMethods(WBMQTT::PMqttRpcServer rpcServer);
    void GetFirmwareInfo(const Json::Value& request,
                         WBMQTT::TMqttRpcServer::TResultCallback onResult,
                         WBMQTT::TMqttRpcServer::TErrorCallback onError);

    void Update(const Json::Value& request,
                WBMQTT::TMqttRpcServer::TResultCallback onResult,
                WBMQTT::TMqttRpcServer::TErrorCallback onError);

    Json::Value ClearError(const Json::Value& request);

    void Restore(const Json::Value& request,
                 WBMQTT::TMqttRpcServer::TResultCallback onResult,
                 WBMQTT::TMqttRpcServer::TErrorCallback onError);

    // Parse common request parameters
    struct TRequestParams
    {
        int SlaveId = 0;
        std::string PortPath;
        std::string Protocol;
    };
    TRequestParams ParseRequestParams(const Json::Value& request);
    Json::Value MakePortRequestJson(const TRequestParams& params);

    // Build GetFirmwareInfo response
    Json::Value BuildFirmwareInfoResponse(const TFwDeviceInfo& deviceInfo);

    // Start a firmware flash operation
    void StartFlash(const TRequestParams& params,
                    const std::string& type,
                    const std::string& fromVersion,
                    const std::string& toVersion,
                    const std::string& fwUrl,
                    bool rebootToBootloader,
                    bool canPreservePortSettings,
                    int componentNumber = -1,
                    const std::string& componentModel = "",
                    std::function<void()> customCompleteCallback = nullptr);

    void StartComponentsFlash(const TRequestParams& params,
                              const TFwDeviceInfo& deviceInfo,
                              const std::string& releaseSuite);

    ITaskRunner& SerialClientTaskRunner;
    WBMQTT::PMqttClient Mqtt;
    std::shared_ptr<TFwDownloader> Downloader;
    PFwUpdateState State;
    std::string ReleaseSuite;

    std::mutex UpdateMutex;
    bool UpdateInProgress = false;
};
