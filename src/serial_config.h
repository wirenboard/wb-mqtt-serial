#pragma once

#include <exception>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "register.h"

#include <wblib/driver_args.h>
#include <wblib/json_utils.h>

#include "confed_protocol_schemas_map.h"
#include "port.h"
#include "rpc/rpc_config.h"
#include "serial_device.h"
#include "templates_map.h"

struct TPortConfig
{
    PPort Port;
    std::vector<PSerialDevice> Devices;
    std::optional<std::chrono::milliseconds> ReadRateLimit;
    std::chrono::microseconds RequestDelay = std::chrono::microseconds::zero();
    TPortOpenCloseLogic::TSettings OpenCloseSettings;

    /**
     * @brief Maximum allowed time from request to response for any device connected to the port.
     * -1 if not set, DefaultResponseTimeout will be used.
     * The timeout is used if device's ResponseTimeout is not set or if device's ResponseTimeout is smaller.
     */
    std::chrono::milliseconds ResponseTimeout = std::chrono::milliseconds(-1);

    bool IsModbusTcp = false;

    void AddDevice(PSerialDevice device);
};

typedef std::shared_ptr<TPortConfig> PPortConfig;

struct THandlerConfig
{
    bool Debug = false;
    WBMQTT::TPublishParameters PublishParameters;
    size_t LowPriorityRegistersRateLimit;
    std::vector<PPortConfig> PortConfigs;

    void AddPortConfig(PPortConfig portConfig);
};

typedef std::shared_ptr<THandlerConfig> PHandlerConfig;

class TConfigParserException: public std::runtime_error
{
public:
    TConfigParserException(const std::string& message);
};

Json::Value LoadConfigTemplatesSchema(const std::string& templateSchemaFileName, const Json::Value& commonDeviceSchema);
void AddRegisterType(Json::Value& configSchema, const std::string& registerType);

typedef std::function<std::pair<PPort, bool>(const Json::Value& config, PRPCConfig rpcConfig)> TPortFactoryFn;

std::pair<PPort, bool> DefaultPortFactory(const Json::Value& port_data, PRPCConfig rpcConfig);

class IRegisterAddressFactory
{
public:
    virtual ~IRegisterAddressFactory() = default;
    /**
     * @brief Load register address from config
     *
     * @param regCfg JSON object representing register configuration
     * @param device_base_address base address of device in global address space
     * @param stride stride of device
     * @param registerByteWidth width of register in bytes
     */
    virtual TRegisterDesc LoadRegisterAddress(const Json::Value& regCfg,
                                              const IRegisterAddress& device_base_address,
                                              uint32_t stride,
                                              uint32_t registerByteWidth) const = 0;

    virtual const IRegisterAddress& GetBaseRegisterAddress() const = 0;
};

class TUint32RegisterAddressFactory: public IRegisterAddressFactory
{
    TUint32RegisterAddress BaseRegisterAddress;
    size_t BytesPerRegister;

public:
    TUint32RegisterAddressFactory(size_t bytesPerRegister = 1);

    TRegisterDesc LoadRegisterAddress(const Json::Value& regCfg,
                                      const IRegisterAddress& deviceBaseAddress,
                                      uint32_t stride,
                                      uint32_t registerByteWidth) const override;

    const IRegisterAddress& GetBaseRegisterAddress() const override;
};

class TStringRegisterAddressFactory: public IRegisterAddressFactory
{
    TStringRegisterAddress BaseRegisterAddress;

public:
    TRegisterDesc LoadRegisterAddress(const Json::Value& regCfg,
                                      const IRegisterAddress& deviceBaseAddress,
                                      uint32_t stride,
                                      uint32_t registerByteWidth) const override;

    const IRegisterAddress& GetBaseRegisterAddress() const override;
};

class IDeviceFactory
{
    std::string CommonDeviceSchemaRef;
    std::string CustomChannelSchemaRef;
    std::unique_ptr<IRegisterAddressFactory> RegisterAddressFactory;

public:
    IDeviceFactory(std::unique_ptr<IRegisterAddressFactory> RegisterAddressFactory,
                   const std::string& commonDeviceSchemaRef,
                   const std::string& customChannelSchemaRef = std::string());

    virtual ~IDeviceFactory() = default;

    /*! Create new device of given type */
    virtual PSerialDevice CreateDevice(const Json::Value& data,
                                       PDeviceConfig deviceConfig,
                                       PPort port,
                                       PProtocol protocol) const = 0;

    const IRegisterAddressFactory& GetRegisterAddressFactory() const;

    /**
     * @brief Get the $ref value of JSONSchema of common device parameters
     *
     * @return JSONSchema $ref. It is used during generation of a JSON schema for wb-mqtt-confed and web UI.
     */
    const std::string& GetCommonDeviceSchemaRef() const;

    /**
     * @brief Get the $ref value of JSONSchema of custom channel
     *
     * @return JSONSchema $ref. It is used during generation of a JSON schema for wb-mqtt-confed and web UI.
     */
    const std::string& GetCustomChannelSchemaRef() const;
};

struct TDeviceConfigLoadParams
{
    std::string DefaultId;
    std::chrono::microseconds DefaultRequestDelay;
    std::chrono::milliseconds PortResponseTimeout;
    std::optional<std::chrono::milliseconds> DefaultReadRateLimit;
    std::string DeviceTemplateTitle;
    const Json::Value* Translations = nullptr;
};

struct TDeviceProtocolParams
{
    PProtocol protocol;
    std::shared_ptr<IDeviceFactory> factory;
};

PDeviceConfig LoadBaseDeviceConfig(const Json::Value& deviceData,
                                   PProtocol protocol,
                                   const IDeviceFactory& factory,
                                   const TDeviceConfigLoadParams& parameters);

struct TLoadRegisterConfigResult
{
    PRegisterConfig RegisterConfig;
    std::string DefaultControlType;
};

TLoadRegisterConfigResult LoadRegisterConfig(const Json::Value& register_data,
                                             const TRegisterTypeMap& type_map,
                                             const std::string& readonly_override_error_message_prefix,
                                             const IDeviceFactory& factory,
                                             const IRegisterAddress& device_base_address,
                                             size_t stride);

class TSerialDeviceFactory
{
    std::unordered_map<std::string, TDeviceProtocolParams> Protocols;

public:
    void RegisterProtocol(PProtocol protocol, IDeviceFactory* deviceFactory);
    PProtocol GetProtocol(const std::string& name);
    TDeviceProtocolParams GetProtocolParams(const std::string& protocolName);
    const std::string& GetCommonDeviceSchemaRef(const std::string& protocolName) const;
    const std::string& GetCustomChannelSchemaRef(const std::string& protocolName) const;
    std::vector<std::string> GetProtocolNames() const;
    PSerialDevice CreateDevice(const Json::Value& device_config,
                               const std::string& defaultId,
                               PPortConfig PPortConfig,
                               TTemplateMap& templates);
};

void RegisterProtocols(TSerialDeviceFactory& deviceFactory);

struct TRegisterBitsAddress
{
    uint32_t Address = 0;
    uint32_t BitWidth = 0;
    uint32_t BitOffset = 0;
};

TRegisterBitsAddress LoadRegisterBitsAddress(const Json::Value& register_data, const std::string& jsonPropertyName);

/*!
 * Basic device factory implementation with uint32 register addresses
 */
template<class Dev, class AddressFactory = TUint32RegisterAddressFactory> class TBasicDeviceFactory
    : public IDeviceFactory
{
public:
    TBasicDeviceFactory(const std::string& commonDeviceSchemaRef,
                        const std::string& customChannelSchemaRef = std::string())
        : IDeviceFactory(std::make_unique<AddressFactory>(), commonDeviceSchemaRef, customChannelSchemaRef)
    {}

    PSerialDevice CreateDevice(const Json::Value& data,
                               PDeviceConfig deviceConfig,
                               PPort port,
                               PProtocol protocol) const override
    {
        auto dev = std::make_shared<Dev>(deviceConfig, port, protocol);
        dev->InitSetupItems();
        return dev;
    }
};

PHandlerConfig LoadConfig(const std::string& configFileName,
                          TSerialDeviceFactory& deviceFactory,
                          const Json::Value& commonDeviceSchema,
                          TTemplateMap& templates,
                          PRPCConfig rpcConfig,
                          const Json::Value& portsSchema,
                          TProtocolConfedSchemasMap& protocolSchemas,
                          TPortFactoryFn portFactory = DefaultPortFactory);

bool IsSubdeviceChannel(const Json::Value& channelSchema);

std::string GetDeviceKey(const std::string& deviceType);
std::string GetSubdeviceSchemaKey(const std::string& subDeviceType);

void AppendParams(Json::Value& dst, const Json::Value& src);
void SetIfExists(Json::Value& dst, const std::string& dstKey, const Json::Value& src, const std::string& srcKey);
std::string DecorateIfNotEmpty(const std::string& prefix,
                               const std::string& str,
                               const std::string& postfix = std::string());
std::string GetProtocolName(const Json::Value& deviceDescription);

namespace SerialConfig
{
    constexpr auto WRITE_ADDRESS_PROPERTY_NAME = "write_address";
    constexpr auto ADDRESS_PROPERTY_NAME = "address";
}

bool HasNoEmptyProperty(const Json::Value& regCfg, const std::string& propertyName);
