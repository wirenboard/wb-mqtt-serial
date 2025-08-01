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

    TDeviceProtocolParams ProtocolParams;
    PSerialDevice Device;
    PDeviceTemplate DeviceTemplate;
    bool DeviceFromConfig = false;
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

    std::chrono::milliseconds ResponseTimeout = DEFAULT_RESPONSE_TIMEOUT;
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
                      const std::string& requestDeviceLoadSchemaFilePath,
                      const std::string& requestDeviceLSetSchemaFilePath,
                      const std::string& requestDeviceProbeSchemaFilePath,
                      const TSerialDeviceFactory& deviceFactory,
                      PTemplateMap templates,
                      TSerialClientTaskRunner& serialClientTaskRunner,
                      TRPCDeviceParametersCache& parametersCache,
                      WBMQTT::PMqttRpcServer rpcServer);

private:
    const TSerialDeviceFactory& DeviceFactory;

    Json::Value RequestDeviceLoadConfigSchema;
    Json::Value RequestDeviceLoadSchema;
    Json::Value RequestDeviceSetSchema;
    Json::Value RequestDeviceProbeSchema;
    PTemplateMap Templates;
    TSerialClientTaskRunner& SerialClientTaskRunner;
    TRPCDeviceParametersCache& ParametersCache;

    void LoadConfig(const Json::Value& request,
                    WBMQTT::TMqttRpcServer::TResultCallback onResult,
                    WBMQTT::TMqttRpcServer::TErrorCallback onError);

    void Load(const Json::Value& request,
              WBMQTT::TMqttRpcServer::TResultCallback onResult,
              WBMQTT::TMqttRpcServer::TErrorCallback onError);

    void Set(const Json::Value& request,
             WBMQTT::TMqttRpcServer::TResultCallback onResult,
             WBMQTT::TMqttRpcServer::TErrorCallback onError);

    void Probe(const Json::Value& request,
               WBMQTT::TMqttRpcServer::TResultCallback onResult,
               WBMQTT::TMqttRpcServer::TErrorCallback onError);
};

typedef std::shared_ptr<TRPCDeviceHandler> PRPCDeviceHandler;
typedef std::vector<std::pair<std::string, PRegister>> TRPCRegisterList;

/**
 * @brief Creates named PRegister map based on template items (channels/parameters) JSON array or object.
 *
 * @param protocolParams - device protocol params for LoadRegisterConfig call
 * @param device - serial device object pointer for TRegister object creation
 * @param templateItems - device template items JSON array or object
 * @param knownItems - known items JSON object, where the key is the item id and the value is the known
 *                     item value, for example: {"baudrate": 96, "in1_mode": 2},
 *                     used to exclule known items from regiter list
 * @param fwVersion - device firmvare version string, used to exclude items unsupporterd by firmware
 *
 * @return TRPCRegisterList - named PRegister list
 */
TRPCRegisterList CreateRegisterList(const TDeviceProtocolParams& protocolParams,
                                    const PSerialDevice& device,
                                    const Json::Value& templateItems,
                                    const Json::Value& knownItems = Json::Value(),
                                    const std::string& fwVersion = std::string());

/**
 * @brief Reads TRPCRegisterList registers and puts valuet to JSON object.
 *
 * @param port - serial port refrence
 * @param device - serial device object pointer
 * @param registerList - named PRegister map
 * @param result - result JSON object reference
 * @param maxRetries - number of request retries in case of error
 */
void ReadRegisterList(TPort& port,
                      PSerialDevice device,
                      TRPCRegisterList& registerList,
                      Json::Value& result,
                      int maxRetries = 0);
