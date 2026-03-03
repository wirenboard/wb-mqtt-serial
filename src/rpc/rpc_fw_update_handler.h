#pragma once

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
    static TRequestParams ParseRequestParams(const Json::Value& request);
    static Json::Value MakePortRequestJson(const TRequestParams& params);

    ITaskRunner& SerialClientTaskRunner;
    WBMQTT::PMqttClient Mqtt;
    std::shared_ptr<TFwDownloader> Downloader;
    PFwUpdateState State;
    std::string ReleaseSuite;

    PFwUpdateLock UpdateLock = std::make_shared<TFwUpdateLock>();
};
