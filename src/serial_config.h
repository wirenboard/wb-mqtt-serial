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
#include "port/feature_port.h"
#include "rpc/rpc_config.h"
#include "serial_device.h"
#include "templates_map.h"

struct TSerialDeviceWithChannels
{
    PSerialDevice Device;
    std::vector<PDeviceChannelConfig> Channels;
};

typedef std::shared_ptr<TSerialDeviceWithChannels> PSerialDeviceWithChannels;

struct TPortConfig
{
    PFeaturePort Port;
    std::vector<PSerialDeviceWithChannels> Devices;
    std::optional<std::chrono::milliseconds> ReadRateLimit;
    std::chrono::microseconds RequestDelay = std::chrono::microseconds::zero();
    TPortOpenCloseLogic::TSettings OpenCloseSettings;

    void AddDevice(PSerialDeviceWithChannels device);
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

typedef std::function<PFeaturePort(const Json::Value& config, PRPCConfig rpcConfig)> TPortFactoryFn;

PFeaturePort DefaultPortFactory(const Json::Value& port_data, PRPCConfig rpcConfig);

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

struct TDeviceLoadDefaults
{
    std::string Id;
    std::chrono::microseconds RequestDelay;
    std::optional<std::chrono::milliseconds> ReadRateLimit;
};

struct TDeviceConfigLoadParams
{
    TDeviceLoadDefaults Defaults;
    std::string DeviceTemplateTitle;
    const Json::Value* Translations = nullptr;
};

struct TDeviceProtocolParams
{
    PProtocol protocol;
    std::shared_ptr<IDeviceFactory> factory;
};

PDeviceConfig LoadDeviceConfig(const Json::Value& dev, PProtocol protocol, const TDeviceConfigLoadParams& parameters);

struct TLoadRegisterConfigResult
{
    PRegisterConfig RegisterConfig;
    std::string DefaultControlType;
};

TLoadRegisterConfigResult LoadRegisterConfig(const Json::Value& registerData,
                                             const TRegisterTypeMap& typeMap,
                                             const std::string& readonlyOverrideErrorMessagePrefix,
                                             const IDeviceFactory& factory,
                                             const IRegisterAddress& deviceBaseAddress,
                                             size_t stride);

class TSerialDeviceFactory
{
    std::unordered_map<std::string, TDeviceProtocolParams> Protocols;

public:
    struct TCreateDeviceParams
    {
        TDeviceLoadDefaults Defaults;
        bool IsModbusTcp = false;
    };

    void RegisterProtocol(PProtocol protocol, IDeviceFactory* deviceFactory);
    TDeviceProtocolParams GetProtocolParams(const std::string& protocolName) const;
    PProtocol GetProtocol(const std::string& protocolName) const;
    const std::string& GetCommonDeviceSchemaRef(const std::string& protocolName) const;
    const std::string& GetCustomChannelSchemaRef(const std::string& protocolName) const;
    std::vector<std::string> GetProtocolNames() const;
    PSerialDeviceWithChannels CreateDevice(const Json::Value& device_config,
                                           const TCreateDeviceParams& params,
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

    PSerialDevice CreateDevice(const Json::Value& data, PDeviceConfig deviceConfig, PProtocol protocol) const override
    {
        return std::make_shared<Dev>(deviceConfig, protocol);
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

void SetIfExists(Json::Value& dst, const std::string& dstKey, const Json::Value& src, const std::string& srcKey);
std::string DecorateIfNotEmpty(const std::string& prefix,
                               const std::string& str,
                               const std::string& postfix = std::string());
std::string GetProtocolName(const Json::Value& deviceDescription);

namespace SerialConfig
{
    constexpr auto WRITE_ADDRESS_PROPERTY_NAME = "write_address";
    constexpr auto ADDRESS_PROPERTY_NAME = "address";
    constexpr auto FW_VERSION_PROPERTY_NAME = "fw";
}

bool HasNoEmptyProperty(const Json::Value& regCfg, const std::string& propertyName);
