#pragma once

#include <exception>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "register.h"

#include <wblib/driver_args.h>
#include <wblib/json_utils.h>

#include "port.h"
#include "serial_device.h"

struct TDeviceTemplate
{
    std::string Type;
    std::string Title;
    Json::Value Schema;

    TDeviceTemplate(const std::string& type, const std::string title, const Json::Value& schema);
};

class ITemplateMap
{
public:
    virtual ~ITemplateMap() = default;

    virtual const TDeviceTemplate& GetTemplate(const std::string& deviceType) = 0;
    virtual std::vector<std::string> GetDeviceTypes() const = 0;
};

class TTemplateMap: public ITemplateMap
{
    /**
     *  @brief Device type to template file path mapping.
     *         Files in map are jsons with device_type parameter, but aren't yet validated against schema.
     */
    std::unordered_map<std::string, std::string> TemplateFiles;

    /**
     * @brief Device type to TDeviceTemplate mapping.
     *        Valid and parsed templates.
     */
    std::unordered_map<std::string, std::shared_ptr<TDeviceTemplate>> ValidTemplates;

    std::unique_ptr<WBMQTT::JSON::TValidator> Validator;

    Json::Value Validate(const std::string& deviceType, const std::string& filePath);
    std::shared_ptr<TDeviceTemplate> GetTemplatePtr(const std::string& deviceType);
    std::string GetDeviceType(const std::string& templatePath) const;

public:
    TTemplateMap() = default;

    /**
     * @brief Construct a new TTemplateMap object.
     *        Throws TConfigParserException if can't open templatesDir.
     *
     * @param templatesDirs directory with templates
     * @param templateSchema JSON Schema for template file validation
     * @param passInvalidTemplates false - throw exception if a folder contains json without device_type parameter
     *                             true - print log message and continue folder processing
     */
    TTemplateMap(const std::string& templatesDir, const Json::Value& templateSchema, bool passInvalidTemplates = true);

    /**
     * @brief Add templates from templatesDir to map.
     *        Throws TConfigParserException if can't open templatesDir.
     *
     * @param templatesDir directory with templates
     * @param passInvalidTemplates false - throw exception if a folder contains json without device_type parameter
     *                             true - print log message and continue folder processing
     */
    void AddTemplatesDir(const std::string& templatesDir, bool passInvalidTemplates = true);

    const TDeviceTemplate& GetTemplate(const std::string& deviceType) override;

    std::vector<std::string> GetDeviceTypes() const override;

    std::vector<std::shared_ptr<TDeviceTemplate>> GetTemplatesOrderedByName();
};

class TSubDevicesTemplateMap: public ITemplateMap
{
    std::unordered_map<std::string, TDeviceTemplate> Templates;
    std::string DeviceType;

public:
    TSubDevicesTemplateMap(const std::string& deviceType, const Json::Value& device);

    const TDeviceTemplate& GetTemplate(const std::string& deviceType) override;

    std::vector<std::string> GetDeviceTypes() const override;

    void AddSubdevices(const Json::Value& subdevicesArray);
};

struct TPortConfig
{
    PPort Port;
    std::vector<PSerialDevice> Devices;
    std::chrono::milliseconds PollInterval = DefaultPollInterval;
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
    std::vector<PPortConfig> PortConfigs;

    void AddPortConfig(PPortConfig portConfig);
};

typedef std::shared_ptr<THandlerConfig> PHandlerConfig;

class TConfigParserException: public std::runtime_error
{
public:
    TConfigParserException(const std::string& message);
};

Json::Value LoadConfigTemplatesSchema(const std::string& templateSchemaFileName, const Json::Value& configSchema);
void AddFakeDeviceType(Json::Value& configSchema);
void AddRegisterType(Json::Value& configSchema, const std::string& registerType);

typedef std::function<std::pair<PPort, bool>(const Json::Value& config)> TPortFactoryFn;

std::pair<PPort, bool> DefaultPortFactory(const Json::Value& port_data);

Json::Value LoadConfigSchema(const std::string& schemaFileName);

//! Register address
struct TRegisterDesc
{
    std::shared_ptr<IRegisterAddress> Address;      //! Register address
    std::shared_ptr<IRegisterAddress> WriteAddress; //! Register address
    uint8_t BitOffset = 0;                          //! Offset of data in register in bits
    uint8_t BitWidth = 0;                           //! Width of data in register in bits
};

class IRegisterAddressFactory
{
public:
    virtual ~IRegisterAddressFactory() = default;
    /**
     * @brief Load register address from config
     *
     * @param regCfg JSON object representing register configuration
     * @param device_base_address base address of device in glodal address space
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
    std::chrono::milliseconds DefaultPollInterval;
    std::string DeviceTemplateTitle;
    const Json::Value* Translations = nullptr;
};

PDeviceConfig LoadBaseDeviceConfig(const Json::Value& deviceData,
                                   PProtocol protocol,
                                   const IDeviceFactory& factory,
                                   const TDeviceConfigLoadParams& parameters);

class TSerialDeviceFactory
{
    std::unordered_map<std::string, std::pair<PProtocol, std::shared_ptr<IDeviceFactory>>> Protocols;

public:
    void RegisterProtocol(PProtocol protocol, IDeviceFactory* deviceFactory);
    PRegisterTypeMap GetRegisterTypes(const std::string& protocolName);
    PSerialDevice CreateDevice(const Json::Value& device_config,
                               const std::string& defaultId,
                               PPortConfig PPortConfig,
                               TTemplateMap& templates);
    PProtocol GetProtocol(const std::string& name);
    const std::string& GetCommonDeviceSchemaRef(const std::string& protocolName) const;
    const std::string& GetCustomChannelSchemaRef(const std::string& protocolName) const;
    std::vector<std::string> GetProtocolNames() const;
};

void RegisterProtocols(TSerialDeviceFactory& deviceFactory);

struct TRegisterBitsAddress
{
    uint32_t Address = 0;
    uint8_t BitWidth = 0;
    uint8_t BitOffset = 0;
};

TRegisterBitsAddress LoadRegisterBitsAddress(const Json::Value& register_data, const std::string& jsonPropertyName);

TRegisterBitsAddress LoadRegisterBitsAddress(const Json::Value& register_data);

/*!
 * Basic device factory implementation with uint32 register addresses
 */
template<class Dev> class TBasicDeviceFactory: public IDeviceFactory
{
public:
    TBasicDeviceFactory(const std::string& commonDeviceSchemaRef,
                        const std::string& customChannelSchemaRef = std::string())
        : IDeviceFactory(std::make_unique<TUint32RegisterAddressFactory>(),
                         commonDeviceSchemaRef,
                         customChannelSchemaRef)
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
                          const Json::Value& baseConfigSchema,
                          TTemplateMap& templates,
                          TPortFactoryFn portFactory = DefaultPortFactory);

bool IsSubdeviceChannel(const Json::Value& channelSchema);

std::string GetDeviceKey(const std::string& deviceType);
std::string GetSubdeviceSchemaKey(const std::string& deviceType, const std::string& subDeviceType);

void AppendParams(Json::Value& dst, const Json::Value& src);
void SetIfExists(Json::Value& dst, const std::string& dstKey, const Json::Value& src, const std::string& srcKey);
std::string DecorateIfNotEmpty(const std::string& prefix,
                               const std::string& str,
                               const std::string& postfix = std::string());
std::string GetProtocolName(const Json::Value& deviceDescription);