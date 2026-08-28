#pragma once
#include "rpc_port_driver_list.h"
#include "templates_map.h"

#include <set>

const std::chrono::seconds DefaultRPCTotalTimeout(10);

class TRPCDeviceParametersCache
{
public:
    TRPCDeviceParametersCache() = default;

    /**
     * Registers DeviceConnectionStateChanged callbacks to remove cached data if device connection lost.
     */
    void RegisterCallbacks(PHandlerConfig handlerConfig);

    /**
     * Creates cache item identifier string based on simplified port description and device address.
     * For example: "/dev/ttyRS485-2:12" or "192.168.18.7:2321:33"
     */
    std::string GetId(const TPort& port, const std::string& slaveId) const;

    /**
     * Puts device parameters data into cache.
     * This method is thread safe.
     */
    void Add(const std::string& id, const Json::Value& value);

    /**
     * Removes device parameters data from cache.
     * This method is thread safe.
     */
    void Remove(const std::string& id);

    /**
     * Returns true if cache contains device parameters data or false otherwise.
     * This method is thread safe.
     */
    bool Contains(const std::string& id) const;

    /**
     * Returns device parameters data if cache contains it or defaultData otherwise.
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
    TRPCDeviceHandler(const std::string& configFileName,
                      const std::string& requestDeviceLoadConfigSchemaFilePath,
                      const std::string& requestDeviceLoadSchemaFilePath,
                      const std::string& requestDeviceLSetSchemaFilePath,
                      const std::string& requestDeviceProbeSchemaFilePath,
                      const std::string& requestDeviceSetPollSchemaFilePath,
                      const TSerialDeviceFactory& deviceFactory,
                      PTemplateMap templates,
                      TSerialClientTaskRunner& serialClientTaskRunner,
                      TRPCDeviceParametersCache& parametersCache,
                      WBMQTT::PMqttRpcServer rpcServer);

private:
    const std::string& ConfigFileName;
    const TSerialDeviceFactory& DeviceFactory;

    Json::Value RequestDeviceLoadConfigSchema;
    Json::Value RequestDeviceLoadSchema;
    Json::Value RequestDeviceSetSchema;
    Json::Value RequestDeviceProbeSchema;
    Json::Value RequestDeviceSetPollSchema;

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

    Json::Value SetPoll(const Json::Value& request);
};

struct TRPCRegister
{
    std::string Id;
    std::string Condition;
    PRegister Register;
    bool CheckUnsupported;
};

typedef std::shared_ptr<TRPCDeviceHandler> PRPCDeviceHandler;
typedef std::vector<TRPCRegister> TRPCRegisterList;

/**
 * @brief Prepares device communication session.
 *
 * @param port - serial port reference
 * @param device - serial device object pointer
 * @param maxRetries - number of request retries in case of error
 */
void PrepareSession(TPort& port, PSerialDevice device, int maxRetries = 0);

//! Ends device communication session, a failure is only logged
void EndSession(TPort& port, PSerialDevice device);

/**
 * @brief Creates named PRegister list based on a template channels JSON array,
 *        every channel must have an "id" set by the caller. Channels without an address
 *        and channels unsupported by the device firmware version are skipped.
 *        Conditions are not evaluated and duplicate declarations of one id are not
 *        merged, the caller selects the acting declarations.
 *        Enums and ranges of Wiren Board device channels are checked for the unsupported
 *        register value 0xFFFE to set the register list item CheckUnsupported flag.
 *
 * @param protocolParams - device protocol params for LoadRegisterConfig call
 * @param device - serial device object pointer for TRegister object creation
 * @param channels - device template channels JSON array
 *
 * @return TRPCRegisterList - named PRegister list
 */
TRPCRegisterList CreateChannelsRegisterList(const TDeviceProtocolParams& protocolParams,
                                            PSerialDevice device,
                                            const Json::Value& channels);

/**
 * @brief Creates named PRegister list based on a template parameters JSON array or object.
 *        Parameters without an address and parameters unsupported by the device firmware
 *        version are skipped. Conditions are not evaluated and condition variants
 *        of one id are not merged, the caller selects the acting declarations.
 *        The fw variants of one id and condition give a single register, the group is read
 *        if any of its declarations is supported by the device firmware version.
 *        Enums and ranges of Wiren Board device parameters are checked for the
 *        unsupported register value 0xFFFE to set the register list item
 *        CheckUnsupported flag.
 *
 * @param protocolParams - device protocol params for LoadRegisterConfig call
 * @param device - serial device object pointer for TRegister object creation
 * @param parameters - device template parameters JSON array or object
 * @param knownValues - parameters with already known values are skipped, the key is
 *                      the parameter id, for example: {"baudrate": 96, "in1_mode": 2}
 * @param ids - ids of the parameters to take, an empty set takes parameters with any id
 *
 * @return TRPCRegisterList - named PRegister list
 */
TRPCRegisterList CreateParametersRegisterList(const TDeviceProtocolParams& protocolParams,
                                              PSerialDevice device,
                                              const Json::Value& parameters,
                                              const Json::Value& knownValues = Json::Value(),
                                              const std::set<std::string>& ids = std::set<std::string>());

/**
 * @brief Reads TRPCRegisterList registers and puts value to JSON object.
 *
 * @param port - serial port reference
 * @param device - serial device object pointer
 * @param registerList - named PRegister map
 * @param maxRetries - number of request retries in case of error
 */
void ReadRegisterList(TPort& port, PSerialDevice device, TRPCRegisterList& registerList, int maxRetries = 0);

Json::Value RawValueToJSON(const TRegisterConfig& reg, TRegisterValue val);

//! Returns true if the register got a value: the device supports it, the read left no error
//! and the value is defined. The read error is also set for values matching the template
//! "error_value" and "unsupported_value" markers
bool RegisterGotValue(const TRPCRegister& item);

//! Adds a value per register id, keeping the members that are not in the register list
void MergeRegisterListValues(const TRPCRegisterList& registerList, Json::Value& values);
