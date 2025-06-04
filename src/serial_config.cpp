#include "serial_config.h"
#include "file_utils.h"
#include "log.h"

#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <sys/sysinfo.h>

#include "tcp_port.h"
#include "tcp_port_settings.h"

#include "serial_port.h"
#include "serial_port_settings.h"

#include "config_merge_template.h"
#include "config_schema_generator.h"

#include "devices/curtains/a_ok_device.h"
#include "devices/curtains/dooya_device.h"
#include "devices/curtains/somfy_sdn_device.h"
#include "devices/curtains/windeco_device.h"
#include "devices/dlms_device.h"
#include "devices/energomera_ce_device.h"
#include "devices/energomera_iec_device.h"
#include "devices/energomera_iec_mode_c_device.h"
#include "devices/iec_mode_c_device.h"
#include "devices/ivtm_device.h"
#include "devices/lls_device.h"
#include "devices/mercury200_device.h"
#include "devices/mercury230_device.h"
#include "devices/milur_device.h"
#include "devices/modbus_device.h"
#include "devices/modbus_io_device.h"
#include "devices/neva_device.h"
#include "devices/pulsar_device.h"
#include "devices/s2k_device.h"
#include "devices/uniel_device.h"

#define LOG(logger) ::logger.Log() << "[serial config] "

using namespace std;
using namespace WBMQTT::JSON;

namespace
{
    const char* DefaultProtocol = "modbus";

    template<class T> T Read(const Json::Value& root, const std::string& key, const T& defaultValue)
    {
        T value;
        if (Get(root, key, value)) {
            return value;
        }
        return defaultValue;
    }

    int GetIntFromString(const std::string& value, const std::string& errorPrefix)
    {
        try {
            return std::stoi(value, /*pos= */ 0, /*base= */ 0);
        } catch (const std::logic_error&) {
            throw TConfigParserException(errorPrefix + ": plain integer or '0x..' hex string expected instead of '" +
                                         value + "'");
        }
    }

    int ToInt(const Json::Value& v, const std::string& title)
    {
        if (v.isInt())
            return v.asInt();
        if (!v.isString()) {
            throw TConfigParserException(title + ": plain integer or '0x..' hex string expected");
        }
        return GetIntFromString(v.asString(), title);
    }

    double ToDouble(const Json::Value& v, const std::string& title)
    {
        if (v.isNumeric())
            return v.asDouble();
        if (!v.isString()) {
            throw TConfigParserException(title + ": number or '0x..' hex string expected");
        }
        return GetIntFromString(v.asString(), title);
    }

    uint64_t ToUint64(const Json::Value& v, const string& title)
    {
        if (v.isUInt())
            return v.asUInt64();
        if (v.isInt()) {
            int val = v.asInt64();
            if (val >= 0) {
                return val;
            }
        }

        if (v.isString()) {
            auto val = v.asString();
            if (val.find("-") == std::string::npos) {
                // don't try to parse strings containing munus sign
                try {
                    return stoull(val, /*pos= */ 0, /*base= */ 0);
                } catch (const logic_error& e) {
                }
            }
        }

        throw TConfigParserException(
            title + ": 32 bit plain unsigned integer (64 bit when quoted) or '0x..' hex string expected instead of '" +
            v.asString() + "'");
    }

    int GetInt(const Json::Value& obj, const std::string& key)
    {
        return ToInt(obj[key], key);
    }

    double GetDouble(const Json::Value& obj, const std::string& key)
    {
        return ToDouble(obj[key], key);
    }

    bool IsSerialNumberChannel(const Json::Value& channel_data)
    {
        const std::vector<std::string> serialNames{"Serial", "serial_number", "Serial NO"};
        return serialNames.end() !=
               std::find(serialNames.begin(), serialNames.end(), channel_data.get("name", std::string()).asString());
    }

    bool ReadChannelsReadonlyProperty(const Json::Value& register_data,
                                      const std::string& key,
                                      bool templateReadonly,
                                      const std::string& override_error_message_prefix,
                                      const std::string& register_type)
    {
        if (!register_data.isMember(key)) {
            return templateReadonly;
        }
        auto& val = register_data[key];
        if (!val.isConvertibleTo(Json::booleanValue)) {
            return templateReadonly;
        }
        bool readonly = val.asBool();
        if (templateReadonly && !readonly) {
            LOG(Warn) << override_error_message_prefix << " unable to make register of type \"" << register_type
                      << "\" writable";
            return true;
        }
        return readonly;
    }

    const TRegisterType& GetRegisterType(const Json::Value& itemData, const TRegisterTypeMap& typeMap)
    {
        if (itemData.isMember("reg_type")) {
            std::string type = itemData["reg_type"].asString();
            try {
                return typeMap.Find(type);
            } catch (...) {
                throw TConfigParserException("invalid register type: " + type);
            }
        }
        return typeMap.GetDefaultType();
    }

    std::optional<std::chrono::milliseconds> GetReadRateLimit(const Json::Value& data)
    {
        std::chrono::milliseconds res(-1);
        try {
            Get(data, "poll_interval", res);
        } catch (...) { // poll_interval is deprecated, so ignore it, if it has wrong format
        }
        Get(data, "read_rate_limit_ms", res);
        if (res < 0ms) {
            return std::nullopt;
        }
        return std::make_optional(res);
    }

    std::optional<std::chrono::milliseconds> GetReadPeriod(const Json::Value& data)
    {
        std::chrono::milliseconds res(-1);
        Get(data, "read_period_ms", res);
        if (res < 0ms) {
            return std::nullopt;
        }
        return std::make_optional(res);
    }

    struct TLoadingContext
    {
        // Full path to loaded item composed from device and channels names
        std::string name_prefix;

        // MQTT topic prefix. It could be different from name_prefix
        std::string mqtt_prefix;
        const IDeviceFactory& factory;
        const IRegisterAddress& device_base_address;
        size_t stride = 0;
        TTitleTranslations translated_name_prefixes;
        const Json::Value* translations = nullptr;

        TLoadingContext(const IDeviceFactory& f, const IRegisterAddress& base_address)
            : factory(f),
              device_base_address(base_address)
        {}
    };

    TTitleTranslations Translate(const std::string& name, bool idIsDefined, const TLoadingContext& context)
    {
        TTitleTranslations res;
        if (context.translations) {
            // Find translation for the name. Iterate through languages
            for (auto it = context.translations->begin(); it != context.translations->end(); ++it) {
                auto lang = it.name();
                auto translatedName = (*it)[name];
                if (!translatedName.isNull()) {
                    // Find prefix translated to the language or english translated prefix
                    auto prefixIt = context.translated_name_prefixes.find(lang);
                    if (prefixIt == context.translated_name_prefixes.end()) {
                        if (lang != "en") {
                            prefixIt = context.translated_name_prefixes.find("en");
                        }
                    }
                    // Take translation of prefix and add translated name
                    if (prefixIt != context.translated_name_prefixes.end()) {
                        res[lang] = prefixIt->second + " " + translatedName.asString();
                        continue;
                    }
                    // There isn't translated prefix
                    // Take MQTT id prefix if any and add translated name
                    if (context.mqtt_prefix.empty()) {
                        res[lang] = translatedName.asString();
                    } else {
                        res[lang] = context.mqtt_prefix + " " + translatedName.asString();
                    }
                }
            }

            for (const auto& it: context.translated_name_prefixes) {
                // There are translatied prefixes, but no translation for the name.
                // Take translated prefix and add the name as is
                if (!res.count(it.first)) {
                    res[it.first] = it.second + " " + name;
                }
            }

            // Name is different from MQTT id and there isn't english translated prefix
            // Take MQTT ID prefix if any and add the name as is
            if (!res.count("en") && idIsDefined) {
                if (context.mqtt_prefix.empty()) {
                    res["en"] = name;
                } else {
                    res["en"] = context.mqtt_prefix + " " + name;
                }
            }
            return res;
        }

        // No translations for names at all.
        // Just compose english name if it is different from MQTT id
        auto trIt = context.translated_name_prefixes.find("en");
        if (trIt != context.translated_name_prefixes.end()) {
            res["en"] = trIt->second + name;
        } else {
            if (idIsDefined) {
                res["en"] = name;
            }
        }
        return res;
    }

    void LoadSimpleChannel(TSerialDeviceWithChannels& deviceWithChannels,
                           const Json::Value& channel_data,
                           const TLoadingContext& context,
                           const TRegisterTypeMap& typeMap)
    {
        std::string mqtt_channel_name(channel_data["name"].asString());
        bool idIsDefined = false;
        if (channel_data.isMember("id")) {
            mqtt_channel_name = channel_data["id"].asString();
            idIsDefined = true;
        }
        if (!context.mqtt_prefix.empty()) {
            mqtt_channel_name = context.mqtt_prefix + " " + mqtt_channel_name;
        }
        auto errorMsgPrefix = "Channel \"" + mqtt_channel_name + "\"";
        std::string default_type_str;
        std::vector<PRegister> registers;
        if (channel_data.isMember("consists_of")) {

            auto read_rate_limit_ms = GetReadRateLimit(channel_data);
            auto read_period = GetReadPeriod(channel_data);

            const Json::Value& reg_data = channel_data["consists_of"];
            for (Json::ArrayIndex i = 0; i < reg_data.size(); ++i) {
                auto reg = LoadRegisterConfig(reg_data[i],
                                              typeMap,
                                              errorMsgPrefix,
                                              context.factory,
                                              context.device_base_address,
                                              context.stride);
                reg.RegisterConfig->ReadRateLimit = read_rate_limit_ms;
                reg.RegisterConfig->ReadPeriod = read_period;
                registers.push_back(deviceWithChannels.Device->AddRegister(reg.RegisterConfig));
                if (!i)
                    default_type_str = reg.DefaultControlType;
                else if (registers[i]->GetConfig()->AccessType != registers[0]->GetConfig()->AccessType)
                    throw TConfigParserException(("can't mix read-only, write-only and writable registers "
                                                  "in one channel -- ") +
                                                 deviceWithChannels.Device->DeviceConfig()->DeviceType);
            }
        } else {
            try {
                auto reg = LoadRegisterConfig(channel_data,
                                              typeMap,
                                              errorMsgPrefix,
                                              context.factory,
                                              context.device_base_address,
                                              context.stride);
                default_type_str = reg.DefaultControlType;
                registers.push_back(deviceWithChannels.Device->AddRegister(reg.RegisterConfig));
            } catch (const std::exception& e) {
                LOG(Warn) << deviceWithChannels.Device->ToString() << " channel \"" + mqtt_channel_name
                          << "\" is ignored: " << e.what();
                return;
            }
        }

        std::string type_str(Read(channel_data, "type", default_type_str));
        if (type_str == "wo-switch" || type_str == "pushbutton") {
            if (type_str == "wo-switch") {
                type_str = "switch";
            }
            for (auto& reg: registers) {
                reg->GetConfig()->AccessType = TRegisterConfig::EAccessType::WRITE_ONLY;
            }
        }

        int order = deviceWithChannels.Channels.size() + 1;
        PDeviceChannelConfig channel(
            new TDeviceChannelConfig(type_str,
                                     deviceWithChannels.Device->DeviceConfig()->Id,
                                     order,
                                     (registers[0]->GetConfig()->AccessType == TRegisterConfig::EAccessType::READ_ONLY),
                                     mqtt_channel_name,
                                     registers));

        for (const auto& it: Translate(channel_data["name"].asString(), idIsDefined, context)) {
            channel->SetTitle(it.second, it.first);
        }

        if (channel_data.isMember("enum") && channel_data.isMember("enum_titles")) {
            const auto& enumValues = channel_data["enum"];
            const auto& enumTitles = channel_data["enum_titles"];
            if (enumValues.size() == enumTitles.size()) {
                for (Json::ArrayIndex i = 0; i < enumValues.size(); ++i) {
                    channel->SetEnumTitles(enumValues[i].asString(),
                                           Translate(enumTitles[i].asString(), true, context));
                }
            } else {
                LOG(Warn) << errorMsgPrefix << ": enum and enum_titles should have the same size -- "
                          << deviceWithChannels.Device->DeviceConfig()->DeviceType;
            }
        }

        if (channel_data.isMember("max")) {
            channel->Max = GetDouble(channel_data, "max");
        }
        if (channel_data.isMember("min")) {
            channel->Min = GetDouble(channel_data, "min");
        }
        if (channel_data.isMember("on_value")) {
            if (registers.size() != 1)
                throw TConfigParserException("on_value is allowed only for single-valued controls -- " +
                                             deviceWithChannels.Device->DeviceConfig()->DeviceType);
            channel->OnValue = std::to_string(GetInt(channel_data, "on_value"));
        }
        if (channel_data.isMember("off_value")) {
            if (registers.size() != 1)
                throw TConfigParserException("off_value is allowed only for single-valued controls -- " +
                                             deviceWithChannels.Device->DeviceConfig()->DeviceType);
            channel->OffValue = std::to_string(GetInt(channel_data, "off_value"));
        }

        if (registers.size() == 1) {
            channel->Precision = registers[0]->GetConfig()->RoundTo;
        }

        Get(channel_data, "units", channel->Units);

        if (IsSerialNumberChannel(channel_data) && registers.size()) {
            deviceWithChannels.Device->SetSnRegister(registers[0]->GetConfig());
        }

        deviceWithChannels.Channels.push_back(channel);
    }

    void LoadChannel(TSerialDeviceWithChannels& deviceWithChannels,
                     const Json::Value& channel_data,
                     const TLoadingContext& context,
                     const TRegisterTypeMap& typeMap);

    void LoadSetupItems(TSerialDevice& device,
                        const Json::Value& item_data,
                        const TRegisterTypeMap& typeMap,
                        const TLoadingContext& context);

    void LoadSubdeviceChannel(TSerialDeviceWithChannels& deviceWithChannels,
                              const Json::Value& channel_data,
                              const TLoadingContext& context,
                              const TRegisterTypeMap& typeMap)
    {
        int shift = 0;
        if (channel_data.isMember("shift")) {
            shift = GetInt(channel_data, "shift");
        }
        std::unique_ptr<IRegisterAddress> baseAddress(context.device_base_address.CalcNewAddress(shift, 0, 0, 0));

        TLoadingContext newContext(context.factory, *baseAddress);
        newContext.translations = context.translations;
        auto name = channel_data["name"].asString();
        newContext.name_prefix = name;
        if (!context.name_prefix.empty()) {
            newContext.name_prefix = context.name_prefix + " " + newContext.name_prefix;
        }

        newContext.mqtt_prefix = name;
        bool idIsDefined = false;
        if (channel_data.isMember("id")) {
            newContext.mqtt_prefix = channel_data["id"].asString();
            idIsDefined = true;
        }

        // Empty id is used if we don't want to add channel name to resulting MQTT topic name
        // This case we also don't add translation to resulting translated channel name
        if (!(idIsDefined && newContext.mqtt_prefix.empty())) {
            newContext.translated_name_prefixes = Translate(name, idIsDefined, context);
        }

        if (!context.mqtt_prefix.empty()) {
            if (newContext.mqtt_prefix.empty()) {
                newContext.mqtt_prefix = context.mqtt_prefix;
            } else {
                newContext.mqtt_prefix = context.mqtt_prefix + " " + newContext.mqtt_prefix;
            }
        }

        newContext.stride = Read(channel_data, "stride", 0);
        LoadSetupItems(*deviceWithChannels.Device, channel_data, typeMap, newContext);

        if (channel_data.isMember("channels")) {
            for (const auto& ch: channel_data["channels"]) {
                LoadChannel(deviceWithChannels, ch, newContext, typeMap);
            }
        }
    }

    void LoadChannel(TSerialDeviceWithChannels& deviceWithChannels,
                     const Json::Value& channel_data,
                     const TLoadingContext& context,
                     const TRegisterTypeMap& typeMap)
    {
        if (channel_data.isMember("enabled") && !channel_data["enabled"].asBool()) {
            if (IsSerialNumberChannel(channel_data)) {
                deviceWithChannels.Device->SetSnRegister(LoadRegisterConfig(channel_data,
                                                                            typeMap,
                                                                            std::string(),
                                                                            context.factory,
                                                                            context.device_base_address,
                                                                            context.stride)
                                                             .RegisterConfig);
            }
            return;
        }

        if (channel_data.isMember("device_type")) {
            LoadSubdeviceChannel(deviceWithChannels, channel_data, context, typeMap);
        } else {
            LoadSimpleChannel(deviceWithChannels, channel_data, context, typeMap);
        }
    }

    void LoadSetupItem(TSerialDevice& device,
                       const Json::Value& item_data,
                       const TRegisterTypeMap& typeMap,
                       const TLoadingContext& context)
    {
        std::string name(Read(item_data, "title", std::string("<unnamed>")));
        if (!context.name_prefix.empty()) {
            name = context.name_prefix + " " + name;
        }
        auto reg = LoadRegisterConfig(item_data,
                                      typeMap,
                                      "Setup item \"" + name + "\"",
                                      context.factory,
                                      context.device_base_address,
                                      context.stride);
        const auto& valueItem = item_data["value"];
        // libjsoncpp uses format "%.17g" in asString() and outputs strings with additional small numbers
        auto value = valueItem.isDouble() ? WBMQTT::StringFormat("%.15g", valueItem.asDouble()) : valueItem.asString();
        device.AddSetupItem(PDeviceSetupItemConfig(
            new TDeviceSetupItemConfig(name, reg.RegisterConfig, value, item_data["id"].asString())));
    }

    void LoadSetupItems(TSerialDevice& device,
                        const Json::Value& device_data,
                        const TRegisterTypeMap& typeMap,
                        const TLoadingContext& context)
    {
        if (device_data.isMember("setup")) {
            for (const auto& setupItem: device_data["setup"])
                LoadSetupItem(device, setupItem, typeMap, context);
        }
    }

    void LoadCommonDeviceParameters(TDeviceConfig& device_config, const Json::Value& device_data)
    {
        if (device_data.isMember("password")) {
            device_config.Password.clear();
            for (const auto& passwordItem: device_data["password"])
                device_config.Password.push_back(ToInt(passwordItem, "password item"));
        }

        if (device_data.isMember("delay_ms")) {
            LOG(Warn) << "\"delay_ms\" is not supported, use \"frame_timeout_ms\" instead";
        }

        Get(device_data, "frame_timeout_ms", device_config.FrameTimeout);
        if (device_config.FrameTimeout.count() < 0) {
            device_config.FrameTimeout = DefaultFrameTimeout;
        }
        Get(device_data, "response_timeout_ms", device_config.ResponseTimeout);
        Get(device_data, "device_timeout_ms", device_config.DeviceTimeout);
        Get(device_data, "device_max_fail_cycles", device_config.DeviceMaxFailCycles);
        Get(device_data, "max_write_fail_time_s", device_config.MaxWriteFailTime);
        Get(device_data, "max_reg_hole", device_config.MaxRegHole);
        Get(device_data, "max_bit_hole", device_config.MaxBitHole);
        Get(device_data, "max_read_registers", device_config.MaxReadRegisters);
        Get(device_data, "min_read_registers", device_config.MinReadRegisters);
        Get(device_data, "max_write_registers", device_config.MaxWriteRegisters);
        Get(device_data, "guard_interval_us", device_config.RequestDelay);
        Get(device_data, "stride", device_config.Stride);
        Get(device_data, "shift", device_config.Shift);
        Get(device_data, "access_level", device_config.AccessLevel);
        Get(device_data, "min_request_interval", device_config.MinRequestInterval);
    }

    void LoadDevice(PPortConfig port_config,
                    const Json::Value& device_data,
                    const std::string& default_id,
                    TTemplateMap& templates,
                    TSerialDeviceFactory& deviceFactory)
    {
        if (device_data.isMember("enabled") && !device_data["enabled"].asBool())
            return;

        port_config->AddDevice(deviceFactory.CreateDevice(device_data, default_id, port_config, templates));
    }

    PPort OpenSerialPort(const Json::Value& port_data, PRPCConfig rpcConfig)
    {
        TSerialPortSettings settings(port_data["path"].asString());

        Get(port_data, "baud_rate", settings.BaudRate);

        if (port_data.isMember("parity"))
            settings.Parity = port_data["parity"].asCString()[0];

        Get(port_data, "data_bits", settings.DataBits);
        Get(port_data, "stop_bits", settings.StopBits);

        PPort port = std::make_shared<TSerialPort>(settings);

        rpcConfig->AddSerialPort(settings);

        return port;
    }

    PPort OpenTcpPort(const Json::Value& port_data, PRPCConfig rpcConfig)
    {
        TTcpPortSettings settings(port_data["address"].asString(), GetInt(port_data, "port"));

        PPort port = std::make_shared<TTcpPort>(settings);

        rpcConfig->AddTCPPort(settings);

        return port;
    }

    void LoadPort(PHandlerConfig handlerConfig,
                  const Json::Value& port_data,
                  const std::string& id_prefix,
                  TTemplateMap& templates,
                  PRPCConfig rpcConfig,
                  TSerialDeviceFactory& deviceFactory,
                  TPortFactoryFn portFactory)
    {
        if (port_data.isMember("enabled") && !port_data["enabled"].asBool())
            return;

        auto port_config = make_shared<TPortConfig>();

        Get(port_data, "response_timeout_ms", port_config->ResponseTimeout);
        Get(port_data, "guard_interval_us", port_config->RequestDelay);
        port_config->ReadRateLimit = GetReadRateLimit(port_data);

        auto port_type = port_data.get("port_type", "serial").asString();

        Get(port_data, "connection_timeout_ms", port_config->OpenCloseSettings.MaxFailTime);
        Get(port_data, "connection_max_fail_cycles", port_config->OpenCloseSettings.ConnectionMaxFailCycles);

        std::tie(port_config->Port, port_config->IsModbusTcp) = portFactory(port_data, rpcConfig);

        const Json::Value& array = port_data["devices"];
        for (Json::Value::ArrayIndex index = 0; index < array.size(); ++index)
            LoadDevice(port_config, array[index], id_prefix + std::to_string(index), templates, deviceFactory);

        handlerConfig->AddPortConfig(port_config);
    }
}

std::string DecorateIfNotEmpty(const std::string& prefix, const std::string& str, const std::string& postfix)
{
    if (str.empty()) {
        return std::string();
    }
    return prefix + str + postfix;
}

void SetIfExists(Json::Value& dst, const std::string& dstKey, const Json::Value& src, const std::string& srcKey)
{
    if (src.isMember(srcKey)) {
        dst[dstKey] = src[srcKey];
    }
}

std::pair<PPort, bool> DefaultPortFactory(const Json::Value& port_data, PRPCConfig rpcConfig)
{
    auto port_type = port_data.get("port_type", "serial").asString();
    if (port_type == "serial") {
        return {OpenSerialPort(port_data, rpcConfig), false};
    }
    if (port_type == "tcp") {
        return {OpenTcpPort(port_data, rpcConfig), false};
    }
    if (port_type == "modbus tcp") {
        return {OpenTcpPort(port_data, rpcConfig), true};
    }
    throw TConfigParserException("invalid port_type: '" + port_type + "'");
}

Json::Value LoadConfigTemplatesSchema(const std::string& templateSchemaFileName, const Json::Value& commonDeviceSchema)
{
    Json::Value schema = WBMQTT::JSON::Parse(templateSchemaFileName);
    AppendParams(schema["definitions"], commonDeviceSchema["definitions"]);
    return schema;
}

void AddRegisterType(Json::Value& configSchema, const std::string& registerType)
{
    configSchema["definitions"]["reg_type"]["enum"].append(registerType);
}

void CheckDuplicatePorts(const THandlerConfig& handlerConfig)
{
    std::unordered_set<std::string> paths;
    for (const auto& port: handlerConfig.PortConfigs) {
        if (!paths.insert(port->Port->GetDescription(false)).second) {
            throw TConfigParserException("Duplicate port: " + port->Port->GetDescription(false));
        }
    }
}

void CheckDuplicateDeviceIds(const THandlerConfig& handlerConfig)
{
    std::unordered_set<std::string> ids;
    for (const auto& port: handlerConfig.PortConfigs) {
        for (const auto& device: port->Devices) {
            if (!ids.insert(device->Device->DeviceConfig()->Id).second) {
                throw TConfigParserException(
                    "Duplicate MQTT device id: " + device->Device->DeviceConfig()->Id +
                    ", set device MQTT ID explicitly to fix (see https://wb.wiki/serial-id-collision)");
            }
        }
    }
}

PHandlerConfig LoadConfig(const std::string& configFileName,
                          TSerialDeviceFactory& deviceFactory,
                          const Json::Value& commonDeviceSchema,
                          TTemplateMap& templates,
                          PRPCConfig rpcConfig,
                          const Json::Value& portsSchema,
                          TProtocolConfedSchemasMap& protocolSchemas,
                          TPortFactoryFn portFactory)
{
    PHandlerConfig handlerConfig(new THandlerConfig);
    Json::Value Root(Parse(configFileName));

    try {
        ValidateConfig(Root, deviceFactory, commonDeviceSchema, portsSchema, templates, protocolSchemas);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error("File: " + configFileName + " error: " + e.what());
    }

    // wb6 - single core - max 100 registers per second
    // wb7 - 4 cores - max 800 registers per second
    handlerConfig->LowPriorityRegistersRateLimit = (1 == get_nprocs_conf()) ? 100 : 800;
    Get(Root, "rate_limit", handlerConfig->LowPriorityRegistersRateLimit);

    Get(Root, "debug", handlerConfig->Debug);

    auto maxUnchangedInterval = DefaultMaxUnchangedInterval;
    Get(Root, "max_unchanged_interval", maxUnchangedInterval);
    if (maxUnchangedInterval.count() >= 0 && maxUnchangedInterval < MaxUnchangedIntervalLowLimit) {
        LOG(Warn) << "\"max_unchanged_interval\" is set to " << MaxUnchangedIntervalLowLimit.count() << " instead of "
                  << maxUnchangedInterval.count();
        maxUnchangedInterval = MaxUnchangedIntervalLowLimit;
    }
    handlerConfig->PublishParameters.Set(maxUnchangedInterval.count());

    const Json::Value& array = Root["ports"];
    for (Json::Value::ArrayIndex index = 0; index < array.size(); ++index) {
        // old default prefix for compat
        LoadPort(handlerConfig,
                 array[index],
                 "wb-modbus-" + std::to_string(index) + "-",
                 templates,
                 rpcConfig,
                 deviceFactory,
                 portFactory);
    }

    CheckDuplicatePorts(*handlerConfig);
    CheckDuplicateDeviceIds(*handlerConfig);

    return handlerConfig;
}

void TPortConfig::AddDevice(PSerialDeviceWithChannels device)
{
    // try to find duplicate of this device
    for (auto dev: Devices) {
        if (dev->Device->Protocol() == device->Device->Protocol()) {
            if (dev->Device->Protocol()->IsSameSlaveId(dev->Device->DeviceConfig()->SlaveId,
                                                       device->Device->DeviceConfig()->SlaveId))
            {
                stringstream ss;
                ss << "id \"" << device->Device->DeviceConfig()->SlaveId << "\" of device \""
                   << device->Device->DeviceConfig()->Name
                   << "\" is already set to device \"" + device->Device->DeviceConfig()->Name + "\"";
                throw TConfigParserException(ss.str());
            }
        }
    }

    Devices.push_back(device);
}

TDeviceChannelConfig::TDeviceChannelConfig(const std::string& type,
                                           const std::string& deviceId,
                                           int order,
                                           bool readOnly,
                                           const std::string& mqttId,
                                           const std::vector<PRegister>& regs)
    : MqttId(mqttId),
      Type(type),
      DeviceId(deviceId),
      Order(order),
      ReadOnly(readOnly),
      Registers(regs)
{}

const std::string& TDeviceChannelConfig::GetName() const
{
    auto it = Titles.find("en");
    if (it != Titles.end()) {
        return it->second;
    }
    return MqttId;
}

const TTitleTranslations& TDeviceChannelConfig::GetTitles() const
{
    return Titles;
}

void TDeviceChannelConfig::SetTitle(const std::string& name, const std::string& lang)
{
    if (!lang.empty()) {
        Titles[lang] = name;
    }
}

const std::map<std::string, TTitleTranslations>& TDeviceChannelConfig::GetEnumTitles() const
{
    return EnumTitles;
}

void TDeviceChannelConfig::SetEnumTitles(const std::string& value, const TTitleTranslations& titles)
{
    if (!value.empty()) {
        EnumTitles[value] = titles;
    }
}

TDeviceSetupItemConfig::TDeviceSetupItemConfig(const std::string& name,
                                               PRegisterConfig reg,
                                               const std::string& value,
                                               const std::string& parameterId)
    : Name(name),
      RegisterConfig(reg),
      Value(value),
      ParameterId(parameterId)
{
    try {
        RawValue = ConvertToRawValue(*reg, Value);
    } catch (const std::exception& e) {
        throw TConfigParserException("\"" + name + "\" bad value \"" + value + "\"");
    }
}

const std::string& TDeviceSetupItemConfig::GetName() const
{
    return Name;
}

const std::string& TDeviceSetupItemConfig::GetValue() const
{
    return Value;
}

const std::string& TDeviceSetupItemConfig::GetParameterId() const
{
    return ParameterId;
}

TRegisterValue TDeviceSetupItemConfig::GetRawValue() const
{
    return RawValue;
}

PRegisterConfig TDeviceSetupItemConfig::GetRegisterConfig() const
{
    return RegisterConfig;
}

void THandlerConfig::AddPortConfig(PPortConfig portConfig)
{
    PortConfigs.push_back(portConfig);
}

TConfigParserException::TConfigParserException(const std::string& message)
    : std::runtime_error("Error parsing config file: " + message)
{}

bool IsSubdeviceChannel(const Json::Value& channelSchema)
{
    return (channelSchema.isMember("oneOf") || channelSchema.isMember("device_type"));
}

void AppendParams(Json::Value& dst, const Json::Value& src)
{
    for (auto it = src.begin(); it != src.end(); ++it) {
        dst[it.name()] = *it;
    }
}

std::string GetProtocolName(const Json::Value& deviceDescription)
{
    std::string p;
    Get(deviceDescription, "protocol", p);
    if (p.empty()) {
        p = DefaultProtocol;
    }
    return p;
}

void LoadChannels(TSerialDeviceWithChannels& deviceWithChannels,
                  const Json::Value& deviceData,
                  const std::optional<std::chrono::milliseconds>& defaultReadRateLimit,
                  const TRegisterTypeMap& typeMap,
                  const TLoadingContext& context)
{
    if (deviceData.isMember("channels")) {
        for (const auto& channel_data: deviceData["channels"]) {
            LoadChannel(deviceWithChannels, channel_data, context, typeMap);
        }
    }

    if (deviceWithChannels.Channels.empty()) {
        LOG(Warn) << "the device has no channels: " + deviceWithChannels.Device->DeviceConfig()->Name;
    }

    auto readRateLimit = GetReadRateLimit(deviceData);
    if (!readRateLimit) {
        readRateLimit = defaultReadRateLimit;
    }
    for (auto channel: deviceWithChannels.Channels) {
        for (auto reg: channel->Registers) {
            if (!reg->GetConfig()->ReadRateLimit) {
                reg->GetConfig()->ReadRateLimit = readRateLimit;
            }
        }
    }
}

void TSerialDeviceFactory::RegisterProtocol(PProtocol protocol, IDeviceFactory* deviceFactory)
{
    TDeviceProtocolParams params = {protocol, std::shared_ptr<IDeviceFactory>(deviceFactory)};
    Protocols.insert(std::make_pair(protocol->GetName(), params));
}

TDeviceProtocolParams TSerialDeviceFactory::GetProtocolParams(const std::string& protocolName) const
{
    auto it = Protocols.find(protocolName);
    if (it == Protocols.end()) {
        throw TSerialDeviceException("unknown protocol: " + protocolName);
    }
    return it->second;
}

PProtocol TSerialDeviceFactory::GetProtocol(const std::string& protocolName) const
{
    return GetProtocolParams(protocolName).protocol;
}

const std::string& TSerialDeviceFactory::GetCommonDeviceSchemaRef(const std::string& protocolName) const
{
    return GetProtocolParams(protocolName).factory->GetCommonDeviceSchemaRef();
}

const std::string& TSerialDeviceFactory::GetCustomChannelSchemaRef(const std::string& protocolName) const
{
    return GetProtocolParams(protocolName).factory->GetCustomChannelSchemaRef();
}

std::vector<std::string> TSerialDeviceFactory::GetProtocolNames() const
{
    std::vector<std::string> res;
    for (const auto& bucket: Protocols) {
        res.emplace_back(bucket.first);
    }
    return res;
}

PSerialDeviceWithChannels TSerialDeviceFactory::CreateDevice(const Json::Value& deviceConfigJson,
                                                             const std::string& defaultId,
                                                             PPortConfig portConfig,
                                                             TTemplateMap& templates)
{
    TDeviceConfigLoadParams loadParams;
    const auto* cfg = &deviceConfigJson;
    unique_ptr<Json::Value> mergedConfig;
    if (deviceConfigJson.isMember("device_type")) {
        auto deviceType = deviceConfigJson["device_type"].asString();
        auto deviceTemplate = templates.GetTemplate(deviceType);
        mergedConfig = std::make_unique<Json::Value>(
            MergeDeviceConfigWithTemplate(deviceConfigJson, deviceType, deviceTemplate->GetTemplate()));
        cfg = mergedConfig.get();
        loadParams.Translations = &deviceTemplate->GetTemplate()["translations"];
    }
    std::string protocolName = DefaultProtocol;
    Get(*cfg, "protocol", protocolName);

    if (portConfig->IsModbusTcp) {
        if (!GetProtocol(protocolName)->IsModbus()) {
            throw TSerialDeviceException("Protocol \"" + protocolName + "\" is not compatible with Modbus TCP");
        }
        protocolName += "-tcp";
    }

    TDeviceProtocolParams protocolParams = GetProtocolParams(protocolName);
    loadParams.DefaultId = defaultId;
    loadParams.DefaultRequestDelay = portConfig->RequestDelay;
    loadParams.PortResponseTimeout = portConfig->ResponseTimeout;
    auto deviceConfig = LoadDeviceConfig(*cfg, protocolParams.protocol, loadParams);
    auto deviceWithChannels = std::make_shared<TSerialDeviceWithChannels>();
    deviceWithChannels->Device =
        protocolParams.factory->CreateDevice(*cfg, deviceConfig, portConfig->Port, protocolParams.protocol);
    TLoadingContext context(*protocolParams.factory,
                            protocolParams.factory->GetRegisterAddressFactory().GetBaseRegisterAddress());
    context.translations = loadParams.Translations;
    auto regTypes = protocolParams.protocol->GetRegTypes();
    LoadSetupItems(*deviceWithChannels->Device, *cfg, *regTypes, context);
    LoadChannels(*deviceWithChannels, *cfg, portConfig->ReadRateLimit, *regTypes, context);

    return deviceWithChannels;
}

IDeviceFactory::IDeviceFactory(std::unique_ptr<IRegisterAddressFactory> registerAddressFactory,
                               const std::string& commonDeviceSchemaRef,
                               const std::string& customChannelSchemaRef)
    : CommonDeviceSchemaRef(commonDeviceSchemaRef),
      CustomChannelSchemaRef(customChannelSchemaRef),
      RegisterAddressFactory(std::move(registerAddressFactory))
{}

const std::string& IDeviceFactory::GetCommonDeviceSchemaRef() const
{
    return CommonDeviceSchemaRef;
}

const std::string& IDeviceFactory::GetCustomChannelSchemaRef() const
{
    return CustomChannelSchemaRef;
}

const IRegisterAddressFactory& IDeviceFactory::GetRegisterAddressFactory() const
{
    return *RegisterAddressFactory;
}

PDeviceConfig LoadDeviceConfig(const Json::Value& dev, PProtocol protocol, const TDeviceConfigLoadParams& parameters)
{
    auto res = std::make_shared<TDeviceConfig>();

    Get(dev, "device_type", res->DeviceType);

    res->Id = Read(dev, "id", parameters.DefaultId);
    Get(dev, "name", res->Name);

    if (dev.isMember("slave_id")) {
        if (dev["slave_id"].isString())
            res->SlaveId = dev["slave_id"].asString();
        else // legacy
            res->SlaveId = std::to_string(dev["slave_id"].asInt());
    }

    LoadCommonDeviceParameters(*res, dev);

    if (res->RequestDelay.count() == 0) {
        res->RequestDelay = parameters.DefaultRequestDelay;
    }

    if (parameters.PortResponseTimeout > res->ResponseTimeout) {
        res->ResponseTimeout = parameters.PortResponseTimeout;
    }
    if (res->ResponseTimeout.count() == -1) {
        res->ResponseTimeout = DefaultResponseTimeout;
    }

    return res;
}

TLoadRegisterConfigResult LoadRegisterConfig(const Json::Value& registerData,
                                             const TRegisterTypeMap& typeMap,
                                             const std::string& readonlyOverrideErrorMessagePrefix,
                                             const IDeviceFactory& factory,
                                             const IRegisterAddress& deviceBaseAddress,
                                             size_t stride)
{
    TLoadRegisterConfigResult res;
    TRegisterType regType = GetRegisterType(registerData, typeMap);
    res.DefaultControlType = regType.DefaultControlType.empty() ? "text" : regType.DefaultControlType;

    if (registerData.isMember("format")) {
        regType.DefaultFormat = RegisterFormatFromName(registerData["format"].asString());
    }

    if (registerData.isMember("word_order")) {
        regType.DefaultWordOrder = WordOrderFromName(registerData["word_order"].asString());
    }

    double scale = Read(registerData, "scale", 1.0); // TBD: check for zero, too
    double offset = Read(registerData, "offset", 0.0);
    double round_to = Read(registerData, "round_to", 0.0);
    TRegisterConfig::TSporadicMode sporadicMode = TRegisterConfig::TSporadicMode::DISABLED;
    if (Read(registerData, "sporadic", false)) {
        sporadicMode = TRegisterConfig::TSporadicMode::ONLY_EVENTS;
    }
    if (Read(registerData, "semi-sporadic", false)) {
        sporadicMode = TRegisterConfig::TSporadicMode::EVENTS_AND_POLLING;
    }

    bool readonly = ReadChannelsReadonlyProperty(registerData,
                                                 "readonly",
                                                 regType.ReadOnly,
                                                 readonlyOverrideErrorMessagePrefix,
                                                 regType.Name);
    // For compatibility with old configs
    readonly = ReadChannelsReadonlyProperty(registerData,
                                            "channel_readonly",
                                            readonly,
                                            readonlyOverrideErrorMessagePrefix,
                                            regType.Name);

    auto registerDesc =
        factory.GetRegisterAddressFactory().LoadRegisterAddress(registerData,
                                                                deviceBaseAddress,
                                                                stride,
                                                                RegisterFormatByteWidth(regType.DefaultFormat));

    if ((regType.DefaultFormat == RegisterFormat::String || regType.DefaultFormat == RegisterFormat::String8) &&
        registerDesc.DataWidth == 0)
    {
        throw TConfigParserException(readonlyOverrideErrorMessagePrefix +
                                     ": String size is not set for register string format");
    }

    res.RegisterConfig = TRegisterConfig::Create(regType.Index,
                                                 registerDesc,
                                                 regType.DefaultFormat,
                                                 scale,
                                                 offset,
                                                 round_to,
                                                 sporadicMode,
                                                 readonly,
                                                 regType.Name,
                                                 regType.DefaultWordOrder);

    if (registerData.isMember("error_value")) {
        res.RegisterConfig->ErrorValue = TRegisterValue{ToUint64(registerData["error_value"], "error_value")};
    }

    if (registerData.isMember("unsupported_value")) {
        res.RegisterConfig->UnsupportedValue =
            TRegisterValue{ToUint64(registerData["unsupported_value"], "unsupported_value")};
    }

    res.RegisterConfig->ReadRateLimit = GetReadRateLimit(registerData);
    res.RegisterConfig->ReadPeriod = GetReadPeriod(registerData);
    return res;
}

void RegisterProtocols(TSerialDeviceFactory& deviceFactory)
{
    TEnergomeraIecWithFastReadDevice::Register(deviceFactory);
    TEnergomeraIecModeCDevice::Register(deviceFactory);
    TIVTMDevice::Register(deviceFactory);
    TLLSDevice::Register(deviceFactory);
    TMercury200Device::Register(deviceFactory);
    TMercury230Device::Register(deviceFactory);
    TMilurDevice::Register(deviceFactory);
    TModbusDevice::Register(deviceFactory);
    TModbusIODevice::Register(deviceFactory);
    TNevaDevice::Register(deviceFactory);
    TPulsarDevice::Register(deviceFactory);
    TS2KDevice::Register(deviceFactory);
    TUnielDevice::Register(deviceFactory);
    TDlmsDevice::Register(deviceFactory);
    Dooya::TDevice::Register(deviceFactory);
    WinDeco::TDevice::Register(deviceFactory);
    Somfy::TDevice::Register(deviceFactory);
    Aok::TDevice::Register(deviceFactory);
    TIecModeCDevice::Register(deviceFactory);
    TEnergomeraCeDevice::Register(deviceFactory);
}

TRegisterBitsAddress LoadRegisterBitsAddress(const Json::Value& register_data, const std::string& jsonPropertyName)
{
    TRegisterBitsAddress res;
    const auto& addressValue = register_data[jsonPropertyName];
    if (addressValue.isString()) {
        const auto& addressStr = addressValue.asString();
        auto pos1 = addressStr.find(':');
        if (pos1 == string::npos) {
            res.Address = GetInt(register_data, jsonPropertyName);
        } else {
            auto pos2 = addressStr.find(':', pos1 + 1);

            res.Address = GetIntFromString(addressStr.substr(0, pos1), jsonPropertyName);
            auto bitOffset = stoul(addressStr.substr(pos1 + 1, pos2));

            if (bitOffset > 255) {
                throw TConfigParserException(
                    "address parsing failed: bit shift must be in range [0, 255] (address string: '" + addressStr +
                    "')");
            }
            res.BitOffset = bitOffset;
            if (pos2 != string::npos) {
                res.BitWidth = stoul(addressStr.substr(pos2 + 1));
            }
        }
    } else {
        res.Address = GetInt(register_data, jsonPropertyName);
    }

    if (register_data.isMember("string_data_size")) {
        res.BitWidth = GetInt(register_data, "string_data_size") * sizeof(char) * 8;
    }
    return res;
}

TUint32RegisterAddressFactory::TUint32RegisterAddressFactory(size_t bytesPerRegister)
    : BaseRegisterAddress(0),
      BytesPerRegister(bytesPerRegister)
{}

TRegisterDesc TUint32RegisterAddressFactory::LoadRegisterAddress(const Json::Value& regCfg,
                                                                 const IRegisterAddress& deviceBaseAddress,
                                                                 uint32_t stride,
                                                                 uint32_t registerByteWidth) const
{
    TRegisterDesc res;

    if (HasNoEmptyProperty(regCfg, SerialConfig::ADDRESS_PROPERTY_NAME)) {
        auto addr = LoadRegisterBitsAddress(regCfg, SerialConfig::ADDRESS_PROPERTY_NAME);
        res.DataOffset = addr.BitOffset;
        res.DataWidth = addr.BitWidth;
        res.Address = std::shared_ptr<IRegisterAddress>(
            deviceBaseAddress.CalcNewAddress(addr.Address, stride, registerByteWidth, BytesPerRegister));
        res.WriteAddress = res.Address;
    }
    if (HasNoEmptyProperty(regCfg, SerialConfig::WRITE_ADDRESS_PROPERTY_NAME)) {
        auto writeAddress = LoadRegisterBitsAddress(regCfg, SerialConfig::WRITE_ADDRESS_PROPERTY_NAME);
        res.WriteAddress = std::shared_ptr<IRegisterAddress>(
            deviceBaseAddress.CalcNewAddress(writeAddress.Address, stride, registerByteWidth, BytesPerRegister));
    }
    return res;
}

const IRegisterAddress& TUint32RegisterAddressFactory::GetBaseRegisterAddress() const
{
    return BaseRegisterAddress;
}

TRegisterDesc TStringRegisterAddressFactory::LoadRegisterAddress(const Json::Value& regCfg,
                                                                 const IRegisterAddress& deviceBaseAddress,
                                                                 uint32_t stride,
                                                                 uint32_t registerByteWidth) const
{
    TRegisterDesc res;
    if (HasNoEmptyProperty(regCfg, SerialConfig::ADDRESS_PROPERTY_NAME)) {
        res.Address = std::make_shared<TStringRegisterAddress>(regCfg[SerialConfig::ADDRESS_PROPERTY_NAME].asString());
        res.WriteAddress = res.Address;
    }
    if (HasNoEmptyProperty(regCfg, SerialConfig::WRITE_ADDRESS_PROPERTY_NAME)) {
        res.WriteAddress =
            std::make_shared<TStringRegisterAddress>(regCfg[SerialConfig::WRITE_ADDRESS_PROPERTY_NAME].asString());
    }
    return res;
}

const IRegisterAddress& TStringRegisterAddressFactory::GetBaseRegisterAddress() const
{
    return BaseRegisterAddress;
}

bool HasNoEmptyProperty(const Json::Value& regCfg, const std::string& propertyName)
{
    return regCfg.isMember(propertyName) &&
           !(regCfg[propertyName].isString() && regCfg[propertyName].asString().empty());
}
