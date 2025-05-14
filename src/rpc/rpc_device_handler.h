#pragma once
#include "rpc_port_driver_list.h"
#include "templates_map.h"

class TRPCDeviceParametersCache
{
public:
    TRPCDeviceParametersCache() = default;

    std::vector<PSerialDevice> Devices;

    void Add(const std::string& id, const Json::Value& value);
    void Remove(const std::string& id);
    bool Contains(const std::string& id) const;
    const Json::Value& Get(const std::string& id, const Json::Value& defaultValue = Json::Value()) const;

private:
    std::mutex Mutex;
    std::unordered_map<std::string, Json::Value> DeviceParameters;
};

class TRPCDeviceHandler
{
public:
    TRPCDeviceHandler(const std::string& requestDeviceLoadConfigSchemaFilePath,
                      const TSerialDeviceFactory& deviceFactory,
                      PTemplateMap templates,
                      TSerialClientTaskRunner& serialClientTaskRunner,
                      WBMQTT::PMqttRpcServer rpcServer);

private:
    const TSerialDeviceFactory& DeviceFactory;

    Json::Value RequestDeviceLoadConfigSchema;
    PTemplateMap Templates;
    TSerialClientTaskRunner& SerialClientTaskRunner;

    TRPCDeviceParametersCache ParametersCache;

    void LoadConfig(const Json::Value& request,
                    WBMQTT::TMqttRpcServer::TResultCallback onResult,
                    WBMQTT::TMqttRpcServer::TErrorCallback onError);
};

typedef std::shared_ptr<TRPCDeviceHandler> PRPCDeviceHandler;
