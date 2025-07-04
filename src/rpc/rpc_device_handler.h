#pragma once
#include "rpc_port_driver_list.h"
#include "templates_map.h"

const std::chrono::seconds DefaultRPCTotalTimeout(10);

class TRPCDeviceParametersCache
{
public:
    TRPCDeviceParametersCache() = default;

    /**
     * Registes DeviceConnectionStateChanged callbacks to remove cached data if device connection lost.
     */
    void RegisterCallbacks(PHandlerConfig handlerConfig);

    /**
     * Creates cache item identifier string based on simlified port description and device address.
     * For example: "/dev/ttyRS485-2:12" or "192.168.18.7:2321:33"
     */
    std::string GetId(const TPort& port, const std::string& slaveId) const;

    /**
     * Puts device paramerers data into cache.
     * This method is thread safe.
     */
    void Add(const std::string& id, const Json::Value& value);

    /**
     * Removes device paramerers data from cache.
     * This method is thread safe.
     */
    void Remove(const std::string& id);

    /**
     * Returns true if cache contains device paramerers data or false overwise.
     * This method is thread safe.
     */
    bool Contains(const std::string& id) const;

    /**
     * Returns device paramerers data if cache contains it or defaultData overwise.
     * This method is thread safe.
     */
    const Json::Value& Get(const std::string& id, const Json::Value& defaultValue = Json::Value()) const;

private:
    mutable std::mutex Mutex;
    std::unordered_map<std::string, Json::Value> DeviceParameters;
};

class TRPCDeviceHelper
{
public:
    TRPCDeviceHelper(const Json::Value& request,
                     const TSerialDeviceFactory& deviceFactory,
                     PTemplateMap templates,
                     TSerialClientTaskRunner& serialClientTaskRunner);

    PSerialClient SerialClient;
    PSerialClientTaskExecutor TaskExecutor;

    TDeviceProtocolParams ProtocolParams;
    PSerialDevice Device;
    PDeviceTemplate DeviceTemplate;
    bool DeviceFromConfig = false;

    void RunTask(PSerialClientTask task);
};

class TRPCDeviceRequest
{
public:
    TRPCDeviceRequest(const TDeviceProtocolParams& protocolParams,
                      PSerialDevice device,
                      PDeviceTemplate deviceTemplate,
                      bool deviceFromConfig);

    TDeviceProtocolParams ProtocolParams;
    PSerialDevice Device;
    PDeviceTemplate DeviceTemplate;
    bool DeviceFromConfig;

    TSerialPortConnectionSettings SerialPortSettings;

    std::chrono::milliseconds ResponseTimeout = DefaultResponseTimeout;
    std::chrono::milliseconds FrameTimeout = DefaultFrameTimeout;
    std::chrono::milliseconds TotalTimeout = DefaultRPCTotalTimeout;

    WBMQTT::TMqttRpcServer::TResultCallback OnResult = nullptr;
    WBMQTT::TMqttRpcServer::TErrorCallback OnError = nullptr;

    void ParseSettings(const Json::Value& request,
                       WBMQTT::TMqttRpcServer::TResultCallback onResult,
                       WBMQTT::TMqttRpcServer::TErrorCallback onError);
};

class TRPCDeviceHandler
{
public:
    TRPCDeviceHandler(const std::string& requestDeviceLoadConfigSchemaFilePath,
                      const std::string& requestDeviceProbeSchemaFilePath,
                      const TSerialDeviceFactory& deviceFactory,
                      PTemplateMap templates,
                      TSerialClientTaskRunner& serialClientTaskRunner,
                      TRPCDeviceParametersCache& parametersCache,
                      WBMQTT::PMqttRpcServer rpcServer);

private:
    const TSerialDeviceFactory& DeviceFactory;

    Json::Value RequestDeviceLoadConfigSchema;
    Json::Value RequestDeviceProbeSchema;
    PTemplateMap Templates;
    TSerialClientTaskRunner& SerialClientTaskRunner;
    TRPCDeviceParametersCache& ParametersCache;

    void LoadConfig(const Json::Value& request,
                    WBMQTT::TMqttRpcServer::TResultCallback onResult,
                    WBMQTT::TMqttRpcServer::TErrorCallback onError);

    void Probe(const Json::Value& request,
               WBMQTT::TMqttRpcServer::TResultCallback onResult,
               WBMQTT::TMqttRpcServer::TErrorCallback onError);
};

typedef std::shared_ptr<TRPCDeviceHandler> PRPCDeviceHandler;
