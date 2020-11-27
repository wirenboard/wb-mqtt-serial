#include "serial_config.h"
#include "log.h"

#include <set>
#include <fstream>
#include <stdexcept>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string>
#include <cstdlib>
#include <memory>

#include "tcp_port_settings.h"
#include "tcp_port.h"

#include "serial_port_settings.h"
#include "serial_port.h"

#define LOG(logger) ::logger.Log() << "[serial config] "

using namespace std;
using namespace WBMQTT::JSON;

namespace WBMQTT
{
    namespace JSON
    {
        template <> inline bool Is<std::chrono::milliseconds>(const Json::Value& value)
        {
            return value.isInt();
        }

        template <> inline std::chrono::milliseconds As<std::chrono::milliseconds>(const Json::Value& value)
        {
            return std::chrono::milliseconds(value.asInt());
        }

        template <> inline bool Is<std::chrono::microseconds>(const Json::Value& value)
        {
            return value.isInt();
        }

        template <> inline std::chrono::microseconds As<std::chrono::microseconds>(const Json::Value& value)
        {
            return std::chrono::microseconds(value.asInt());
        }
    }
}

namespace {
    const char* DefaultProtocol = "modbus";

    bool EndsWith(const string& str, const string& suffix)
    {
        return str.size() >= suffix.size() && str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    template <class T> T Read(const Json::Value& root, const std::string& key, const T& defaultValue) {
        T value;
        if (Get(root, key, value)) {
            return value;
        }
        return defaultValue;
    }

    int ToInt(const Json::Value& v, const std::string& title)
    {
        if (v.isInt())
            return v.asInt();

        if (v.isString()) {
            try {
                return std::stoi(v.asString(), /*pos= */ 0, /*base= */ 0);
            } catch (const std::logic_error& e) {}
        }

        throw TConfigParserException(
            title + ": plain integer or '0x..' hex string expected instead of '" + v.asString() +
            "'");
        // v.asString() should give a bit more information what config this exception came from
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
                } catch (const logic_error& e) {}
            }
        }

        throw TConfigParserException(
            title + ": 32 bit plain unsigned integer (64 bit when quoted) or '0x..' hex string expected instead of '" + v.asString() +
            "'");
    }

    int GetInt(const Json::Value& obj, const std::string& key)
    {
        return ToInt(obj[key], key);
    }

    bool ReadChannelsReadonlyProperty(const Json::Value& register_data,
                                      const std::string& key,
                                      bool templateReadonly,
                                      const std::string& channel_name,
                                      const std::string& register_type)
    {
        bool readonly = templateReadonly;
        if (Get(register_data, key, readonly)) {
            if (templateReadonly && !readonly) {
                LOG(Warn) << "Channel \"" << channel_name << "\" unable to make register of type \"" << register_type << "\" writable";
                return true;
            }
        }
        return readonly;
    }

    PRegisterConfig LoadRegisterConfig(PDeviceConfig device_config,
                                       const Json::Value& register_data,
                                       std::string& default_type_str,
                                       const std::string& channel_name)
    {
        int address, bit_offset = 0, bit_width = 0;
        {
            const auto & addressValue = register_data["address"];
            if (addressValue.isString()) {
                const auto & addressStr = addressValue.asString();
                auto pos1 = addressStr.find(':');
                if (pos1 == string::npos) {
                    address = GetInt(register_data, "address");
                } else {
                    auto pos2 = addressStr.find(':', pos1 + 1);

                    address = stoi(addressStr.substr(0, pos1));
                    bit_offset = stoi(addressStr.substr(pos1 + 1, pos2));

                    if (bit_offset < 0 || bit_offset > 255) {
                        throw TConfigParserException("channel \"" + channel_name 
                                                      + "\" error during address parsing: bit shift must be in range [0, 255] (address string: '" 
                                                      + addressStr + "')");
                    }

                    if (pos2 != string::npos) {
                        bit_width = stoi(addressStr.substr(pos2 + 1));
                        if (bit_width < 0 || bit_width > 64) {
                            throw TConfigParserException("channel \"" + channel_name 
                                                      + "\" error during address parsing: bit count must be in range [0, 64] (address string: '"
                                                      + addressStr + "')");
                        }
                    }
                }
            } else {
                address = GetInt(register_data, "address");
            }
        }

        string reg_type_str = register_data["reg_type"].asString();
        default_type_str = "text";
        auto it = device_config->TypeMap->find(reg_type_str);
        if (it == device_config->TypeMap->end())
            throw TConfigParserException("channel \"" + channel_name + "\" invalid register type: " + reg_type_str + " -- " + device_config->DeviceType);
        if (!it->second.DefaultControlType.empty())
            default_type_str = it->second.DefaultControlType;

        RegisterFormat format = register_data.isMember("format") ?
            RegisterFormatFromName(register_data["format"].asString()) :
            it->second.DefaultFormat;

        EWordOrder word_order = register_data.isMember("word_order") ?
            WordOrderFromName(register_data["word_order"].asString()) :
            it->second.DefaultWordOrder;

        double scale        = Read(register_data, "scale",    1.0); // TBD: check for zero, too
        double offset       = Read(register_data, "offset",   0);
        double round_to     = Read(register_data, "round_to", 0.0);

        bool readonly = ReadChannelsReadonlyProperty(register_data, "readonly", it->second.ReadOnly, channel_name, it->second.Name);
        // For comptibility with old configs
        readonly = ReadChannelsReadonlyProperty(register_data, "channel_readonly", readonly, channel_name, it->second.Name);

        std::unique_ptr<uint64_t> error_value;
        if (register_data.isMember("error_value")) {
            error_value = std::make_unique<uint64_t>(ToUint64(register_data["error_value"], "error_value"));
        }

        std::unique_ptr<uint64_t> unsupported_value;
        if (register_data.isMember("unsupported_value")) {
            unsupported_value = std::make_unique<uint64_t>(ToUint64(register_data["unsupported_value"], "unsupported_value"));
        }

        PRegisterConfig reg = TRegisterConfig::Create(
            it->second.Index, address, format, scale, offset,
            round_to, true, readonly, it->second.Name, std::move(error_value),
            word_order, bit_offset, bit_width, std::move(unsupported_value));
        
        Get(register_data, "poll_interval", reg->PollInterval);
        return reg;
    }

    void LoadChannel(PDeviceConfig device_config, const Json::Value& channel_data)
    {
        if (channel_data.isMember("enabled") && !channel_data["enabled"].asBool()) {
            return;
        }
        std::string name = channel_data["name"].asString();
        std::string default_type_str;
        std::vector<PRegisterConfig> registers;
        if (channel_data.isMember("consists_of")) {

            std::chrono::milliseconds poll_interval(Read(channel_data, "poll_interval", std::chrono::milliseconds(-1)));

            const Json::Value& reg_data = channel_data["consists_of"];
            for(Json::ArrayIndex i = 0; i < reg_data.size(); ++i) {
                std::string def_type;
                auto reg = LoadRegisterConfig(device_config, reg_data[i], def_type, name);
                /* the poll_interval specified for the specific register has a precedence over the one specified for the compound channel */
                if ((reg->PollInterval.count() < 0) && (poll_interval.count() >= 0))
                    reg->PollInterval = poll_interval;
                registers.push_back(reg);
                if (!i)
                    default_type_str = def_type;
                else if (registers[i]->ReadOnly != registers[0]->ReadOnly)
                    throw TConfigParserException(("can't mix read-only and writable registers "
                                                "in one channel -- ") + device_config->DeviceType);
            }
        } else {
            registers.push_back(LoadRegisterConfig(device_config, channel_data, default_type_str, name));
        }

        std::string type_str(Read(channel_data, "type", default_type_str));
        if (type_str == "wo-switch") {
            type_str = "switch";
            for (auto& reg: registers)
                reg->Poll = false;
        }

        std::string on_value;
        if (channel_data.isMember("on_value")) {
            if (registers.size() != 1)
                throw TConfigParserException("can only use on_value for single-valued controls -- " +
                                            device_config->DeviceType);
            on_value = std::to_string(GetInt(channel_data, "on_value"));
        }

        int max = -1;
        if (channel_data.isMember("max"))
            max = GetInt(channel_data, "max");

        int order        = device_config->NextOrderValue();
        PDeviceChannelConfig channel(new TDeviceChannelConfig(name, type_str, device_config->Id, order,
                                                on_value, max, registers[0]->ReadOnly,
                                                registers));
        device_config->AddChannel(channel);
    }

    void SetPropertyWithNotification(Json::Value& dst, Json::Value::const_iterator itProp, const std::string& logPrefix) {
        if (dst.isMember(itProp.name()) && dst[itProp.name()] != *itProp) {
            LOG(Info) << logPrefix << " override property \"" << itProp.name() << "\"";
        }
        dst[itProp.name()] = *itProp;
    }

    void MergeChannelProperties(Json::Value& dst, const Json::Value& src, const std::string& logPrefix)
    {
        for (auto itProp = src.begin(); itProp != src.end(); ++itProp) {
            if (itProp.name() == "poll_interval" || itProp.name() == "enabled") {
                SetPropertyWithNotification(dst, itProp, logPrefix);
                continue;
            }
            if (itProp.name() == "readonly") {
                if ((itProp ->asString() != "true") && (dst.isMember(itProp.name())) && (dst[itProp.name()].asString() == "true")) {
                    LOG(Warn) << logPrefix << " can't override property \"" << itProp.name() << "\"";
                    continue;
                }
                SetPropertyWithNotification(dst, itProp, logPrefix);
                continue;
            }
            if (itProp.name() == "name") {
                continue;
            }
            LOG(Warn) << logPrefix << " can't override property \"" << itProp.name() << "\"";
        }
    }

    void UpdateChannels(Json::Value& dst, const Json::Value& src, const std::string& logPrefix)
    {
        std::unordered_map<std::string, Json::ArrayIndex> channelNames;

        for (Json::ArrayIndex i = 0; i < dst.size(); ++i) {
            channelNames.emplace(dst[i]["name"].asString(), i);
        }

        for (const auto& elem: src) {
            string channelName(elem["name"].asString());
            if (channelNames.count(channelName)) {
                MergeChannelProperties(dst[channelNames[channelName]], elem, logPrefix + " channel \"" + channelName + "\"");
            } else {
                dst.append(elem);
            }
        }
    }

    void AppendUserData(Json::Value& dst, const Json::Value& src, const std::string& deviceName)
    {
        for (auto itProp = src.begin(); itProp != src.end(); ++itProp) {
            if (itProp.name() != "channels") {
                SetPropertyWithNotification(dst, itProp, deviceName);
            }
        }
        UpdateChannels(dst["channels"], src["channels"], "\"" + deviceName + "\"");
    }

    void LoadSetupItem(PDeviceConfig device_config, const Json::Value& item_data)
    {
        int address = GetInt(item_data, "address");
        std::string reg_type_str = item_data["reg_type"].asString();
        int type = 0;
        if (!reg_type_str.empty()) {
            auto it = device_config->TypeMap->find(reg_type_str);
            if (it == device_config->TypeMap->end())
                throw TConfigParserException("invalid setup register type: " +
                                            reg_type_str + " -- " + device_config->DeviceType);
            type = it->second.Index;
        }
        RegisterFormat format = U16;
        if (item_data.isMember("format"))
            format = RegisterFormatFromName(item_data["format"].asString());
        PRegisterConfig reg = TRegisterConfig::Create(
            type, address, format, 1, 0, 0, true, true, "<unspec>");

        int value = GetInt(item_data, "value");
        std::string name(Read(item_data, "title", std::string("<unnamed>")));
        device_config->AddSetupItem(PDeviceSetupItemConfig(new TDeviceSetupItemConfig(name, reg, value)));
    }

    bool LoadDeviceTemplatableConfigPart(PDeviceConfig device_config, const Json::Value& device_data, TSerialDeviceFactory& deviceFactory, bool modbusTcpWorkaround)
    {
        Get(device_data, "protocol", device_config->Protocol);
        if (device_config->Protocol.empty()) {
            device_config->Protocol = DefaultProtocol;
        }

        if (modbusTcpWorkaround) {
            if (deviceFactory.GetProtocol(device_config->Protocol)->IsModbus()) {
                device_config->Protocol += "-tcp";
            } else {
                LOG(Warn) << "Device \"" << device_config->Name << "\": protocol \"" + device_config->Protocol + "\" is not compatible with Modbus TCP";
            }
        }

        device_config->TypeMap = deviceFactory.GetRegisterTypes(device_config);

        if (device_data.isMember("setup")) {
            for(const auto& setupItem: device_data["setup"])
                LoadSetupItem(device_config, setupItem);
        }

        if (device_data.isMember("password")) {
            device_config->Password.clear();
            for(const auto& passwordItem: device_data["password"])
                device_config->Password.push_back(ToInt(passwordItem, "password item"));
        }

        if (device_data.isMember("delay_ms")) {
            LOG(Warn) << "\"delay_ms\" is not supported, use \"frame_timeout_ms\" instead";
        }

        Get(device_data, "frame_timeout_ms",       device_config->FrameTimeout);
        if (device_config->FrameTimeout.count() < 0) {
            device_config->FrameTimeout = std::chrono::milliseconds::zero();
        }
        Get(device_data, "response_timeout_ms",    device_config->ResponseTimeout);
        Get(device_data, "device_timeout_ms",      device_config->DeviceTimeout);
        Get(device_data, "device_max_fail_cycles", device_config->DeviceMaxFailCycles);
        Get(device_data, "max_reg_hole",           device_config->MaxRegHole);
        Get(device_data, "max_bit_hole",           device_config->MaxBitHole);
        Get(device_data, "max_read_registers",     device_config->MaxReadRegisters);
        Get(device_data, "guard_interval_us",      device_config->RequestDelay);
        Get(device_data, "stride",                 device_config->Stride);
        Get(device_data, "shift",                  device_config->Shift);
        Get(device_data, "access_level",           device_config->AccessLevel);

        if (device_data.isMember("channels")) {
            for (const auto& channel_data: device_data["channels"]) {
                LoadChannel(device_config, channel_data);
            }
        }
        return true;
    }

    void LoadDevice(PPortConfig port_config,
                    const Json::Value& device_data,
                    const std::string& default_id,
                    TTemplateMap& templates,
                    TSerialDeviceFactory& deviceFactory)
    {
        if (device_data.isMember("enabled") && !device_data["enabled"].asBool())
            return;

        PDeviceConfig device_config = std::make_shared<TDeviceConfig>();
        device_config->Id = Read(device_data, "id",  default_id);
        Get(device_data, "name", device_config->Name);

        if (device_data["slave_id"].isString())
            device_config->SlaveId = device_data["slave_id"].asString();
        else // legacy
            device_config->SlaveId = std::to_string(device_data["slave_id"].asInt());

        auto device_poll_interval = std::chrono::milliseconds(-1);
        Get(device_data, "poll_interval", device_poll_interval);

        if (device_data.isMember("device_type")) {
            device_config->DeviceType = device_data["device_type"].asString();
            auto tmpl = templates.GetTemplate(device_config->DeviceType);
            if (tmpl.isMember("name")) {
                if (device_config->Name == "") {
                    device_config->Name = tmpl["name"].asString() + " " + device_config->SlaveId;
                }
            } else {
                if (device_config->Name == "") {
                    throw TConfigParserException(
                        "Property name is missing in " + device_config->DeviceType + " template");
                }
            }

            if (tmpl.isMember("id")) {
                if (device_config->Id == default_id)
                    device_config->Id = tmpl["id"].asString() + "_" + device_config->SlaveId;
            }
            AppendUserData(tmpl, device_data, device_config->Name);
            LoadDeviceTemplatableConfigPart(device_config, tmpl, deviceFactory, port_config->IsModbusTcp);
        } else {
            LoadDeviceTemplatableConfigPart(device_config, device_data, deviceFactory, port_config->IsModbusTcp);
        }
        if (device_config->DeviceChannelConfigs.empty())
            throw TConfigParserException("the device has no channels: " + device_config->Name);

        if (device_config->RequestDelay.count() == 0) {
            device_config->RequestDelay = port_config->RequestDelay;
        }

        if (port_config->ResponseTimeout > device_config->ResponseTimeout) {
            device_config->ResponseTimeout = port_config->ResponseTimeout;
        }
        if (device_config->ResponseTimeout.count() == -1) {
            device_config->ResponseTimeout = DefaultResponseTimeout;
        }

        port_config->AddDeviceConfig(device_config, deviceFactory);
        for (auto channel: device_config->DeviceChannelConfigs) {
            for (auto reg: channel->RegisterConfigs) {
                if (reg->PollInterval.count() < 0) {
                    reg->PollInterval = ((device_poll_interval.count() >= 0) ? device_poll_interval : port_config->PollInterval);
                }
            }
        }
    }

    PPort OpenSerialPort(const Json::Value& port_data)
    {
        TSerialPortSettings settings(port_data["path"].asString());

        Get(port_data, "baud_rate", settings.BaudRate);

        if (port_data.isMember("parity"))
            settings.Parity = port_data["parity"].asCString()[0];

        Get(port_data, "data_bits", settings.DataBits);
        Get(port_data, "stop_bits", settings.StopBits);

        return std::make_shared<TSerialPortWithIECHack>(std::make_shared<TSerialPort>(settings));
    }

    PPort OpenTcpPort(const Json::Value& port_data)
    {
        TTcpPortSettings settings(port_data["address"].asString(), GetInt(port_data, "port"));

        Get(port_data, "connection_timeout_ms",      settings.ConnectionTimeout);
        Get(port_data, "connection_max_fail_cycles", settings.ConnectionMaxFailCycles);

        return std::make_shared<TTcpPort>(settings);
    }

    void LoadPort(PHandlerConfig handlerConfig,
                  const Json::Value& port_data,
                  const std::string& id_prefix,
                  TTemplateMap& templates,
                  TSerialDeviceFactory& deviceFactory,
                  TPortFactoryFn portFactory)
    {
        if (port_data.isMember("enabled") && !port_data["enabled"].asBool())
            return;

        auto port_config = make_shared<TPortConfig>();

        Get(port_data, "response_timeout_ms", port_config->ResponseTimeout);
        Get(port_data, "poll_interval",       port_config->PollInterval);
        Get(port_data, "guard_interval_us",   port_config->RequestDelay);

        auto port_type = port_data.get("port_type", "serial").asString();

        std::tie(port_config->Port, port_config->IsModbusTcp) = portFactory(port_data);


        const Json::Value& array = port_data["devices"];
        for(Json::Value::ArrayIndex index = 0; index < array.size(); ++index)
            LoadDevice(port_config, array[index], id_prefix + std::to_string(index), templates, deviceFactory);

        handlerConfig->AddPortConfig(port_config);
    }
}

std::pair<PPort, bool> DefaultPortFactory(const Json::Value& port_data)
{
    auto port_type = port_data.get("port_type", "serial").asString();
    if (port_type == "serial") {
        return {OpenSerialPort(port_data), false};
    }
    if (port_type == "tcp") {
        return {OpenTcpPort(port_data), false};
    }
    if (port_type == "modbus tcp") {
        return {OpenTcpPort(port_data), true};
    }
    throw TConfigParserException("invalid port_type: '" + port_type + "'");
}

TTemplateMap::TTemplateMap(const std::string& templatesDir, const Json::Value& templateSchema): 
    Validator(new  WBMQTT::JSON::TValidator(templateSchema)) 
{
    AddTemplatesDir(templatesDir);
}

void TTemplateMap::AddTemplatesDir(const std::string& templatesDir)
{
    DIR *dir;
    struct dirent *dirp;
    struct stat filestat;

    if ((dir = opendir(templatesDir.c_str())) == NULL)
        throw TConfigParserException("Cannot open " + templatesDir + " directory");

    while ((dirp = readdir(dir))) {
        std::string dname = dirp->d_name;
        if(!EndsWith(dname, ".json"))
            continue;

        std::string filepath = templatesDir + "/" + dname;
        if (stat(filepath.c_str(), &filestat)) {
            continue;
        }
        if (S_ISDIR(filestat.st_mode)) {
            continue;
        }

        try {
            Json::Value root = WBMQTT::JSON::Parse(filepath);
            TemplateFiles[root["device_type"].asString()] = filepath;
        } catch (const std::exception& e) {
            LOG(Error) << "Failed to parse " << filepath << "\n" << e.what();
            continue;
        }
    }
    closedir(dir);
}

const Json::Value& TTemplateMap::GetTemplate(const std::string& deviceType) 
{
    if (!Validator) {
        throw std::runtime_error("Can't find validator for device templates");
    }
    try {
        return ValidTemplates.at(deviceType);
    } catch ( const std::out_of_range& ) {
        std::string filePath;
        try {
            filePath = TemplateFiles.at(deviceType);
        } catch ( const std::out_of_range& ) {
            throw std::runtime_error("Can't find template for " + deviceType);
        }
        Json::Value root(WBMQTT::JSON::Parse(filePath));
        try {
            Validator->Validate(root);
        } catch (const std::runtime_error& e) {
            throw std::runtime_error("File: " + filePath + " error: " + e.what());
        }
        TemplateFiles.erase(filePath);
        ValidTemplates.emplace(filePath, root["device"]);
        return ValidTemplates[filePath];
    }
}

const std::map<std::string, Json::Value>& TTemplateMap::GetTemplates()
{
    if (!Validator) {
        throw std::runtime_error("Can't find validator for device templates");
    }

    for (const auto& file: TemplateFiles) {
        Json::Value root(WBMQTT::JSON::Parse(file.second));
        try {
            Validator->Validate(root);
        } catch (const std::runtime_error& e) {
            throw std::runtime_error("File: " + file.second + " error: " + e.what());
        }
        Json::Value v(root["device"]);
        if (root.isMember("setup_schema")) {
            v["setup_schema"] = root["setup_schema"];
        }
        ValidTemplates.emplace(file.first, v);
    }
    TemplateFiles.clear();
    return ValidTemplates;
}

Json::Value LoadConfigTemplatesSchema(const std::string& templateSchemaFileName, const Json::Value& configSchema)
{
    Json::Value schema = WBMQTT::JSON::Parse(templateSchemaFileName);
    schema["definitions"] = configSchema["definitions"];
    return schema;
}

void AddProtocolType(Json::Value& configSchema, const std::string& protocolType)
{
    configSchema["definitions"]["deviceProtocol"]["enum"].append(protocolType);
}

void AddRegisterType(Json::Value& configSchema, const std::string& registerType)
{
    configSchema["definitions"]["reg_type"]["enum"].append(registerType);
}

Json::Value LoadConfigSchema(const std::string& schemaFileName)
{
    return Parse(schemaFileName);
}

PHandlerConfig LoadConfig(const std::string& configFileName,
                          TSerialDeviceFactory& deviceFactory,
                          const Json::Value& configSchema,
                          TTemplateMap& templates,
                          TPortFactoryFn portFactory)
{
    PHandlerConfig handlerConfig(new THandlerConfig);
    Json::Value Root(Parse(configFileName));

    try {
        Validate(Root, configSchema);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error("File: " + configFileName + " error: " + e.what());
    }

    Get(Root, "debug", handlerConfig->Debug);
    Get(Root, "max_unchanged_interval", handlerConfig->MaxUnchangedInterval);

    const Json::Value& array = Root["ports"];
    for(Json::Value::ArrayIndex index = 0; index < array.size(); ++index) {
        // XXX old default prefix for compat
        LoadPort(handlerConfig, array[index], "wb-modbus-" + std::to_string(index) + "-", templates, deviceFactory, portFactory);
    }

    // check are there any devices defined
    for (const auto& port_config : handlerConfig->PortConfigs) {
        if (!port_config->DeviceConfigs.empty()) { // found one
            return handlerConfig;
        }
    }

    throw TConfigParserException("no devices defined in config. Nothing to do");
}

PHandlerConfig LoadConfig(const std::string& configFileName,
                          TSerialDeviceFactory& deviceFactory,
                          const Json::Value& configSchema,
                          TPortFactoryFn portFactory)
{
    TTemplateMap t;
    return LoadConfig(configFileName, deviceFactory, configSchema, t, portFactory);
}

void TPortConfig::AddDeviceConfig(PDeviceConfig device_config, TSerialDeviceFactory& deviceFactory)
{
    // try to find duplicate of this device
    for (PDeviceConfig dev : DeviceConfigs) {
        if (dev->Protocol == device_config->Protocol)
        {
            auto protocol = deviceFactory.GetProtocol(dev->Protocol);
            if (protocol->IsSameSlaveId(dev->SlaveId, device_config->SlaveId)) {
                throw TConfigParserException("device redefinition: " + device_config->Protocol + ":" + device_config->SlaveId);
            }
        }
    }

    DeviceConfigs.push_back(device_config);
}

TDeviceChannelConfig::TDeviceChannelConfig(const std::string& name,
                                           const std::string& type,
                                           const std::string& deviceId,
                                           int                order,
                                           const std::string& onValue,
                                           int max,
                                           bool readOnly,
                                           const std::vector<PRegisterConfig> regs)
    : Name(name), Type(type), DeviceId(deviceId),
      Order(order), OnValue(onValue), Max(max),
      ReadOnly(readOnly), RegisterConfigs(regs) 
{}

TDeviceSetupItemConfig::TDeviceSetupItemConfig(const std::string& name, PRegisterConfig reg, int value)
        : Name(name), RegisterConfig(reg), Value(value)
{}

TDeviceConfig::TDeviceConfig(const std::string& name, const std::string& slave_id, const std::string& protocol)
        : Name(name), SlaveId(slave_id), Protocol(protocol) 
{}

int TDeviceConfig::NextOrderValue() const 
{
    return DeviceChannelConfigs.size() + 1;
}

void TDeviceConfig::AddChannel(PDeviceChannelConfig channel)
{
    DeviceChannelConfigs.push_back(channel); 
}

void TDeviceConfig::AddSetupItem(PDeviceSetupItemConfig item) 
{
    SetupItemConfigs.push_back(item);
}

void THandlerConfig::AddPortConfig(PPortConfig portConfig) 
{
    portConfig->MaxUnchangedInterval = MaxUnchangedInterval;
    PortConfigs.push_back(portConfig);
}

TConfigParserException::TConfigParserException(const std::string& message)
    : std::runtime_error("Error parsing config file: " + message) 
{}

Json::Value RemoveReadonlyProperties(const Json::Value& value)
{
    Json::Value res;
    res["name"] = value["name"];
    if (value.isMember("poll_interval")) {
        res["poll_interval"] = value["poll_interval"];
    } else  {
        res["poll_interval"] = 20;
    }
    if (value.isMember("enabled")) {
        res["enabled"] = value["enabled"];
    } else {
        res["enabled"] = true;
    }
    return res;
}

std::pair<Json::Value, Json::Value> SplitChannels(Json::Value& device, TTemplateMap& templates)
{
    Json::Value channels(Json::arrayValue);
    if (!device.isMember("device_type")) {
        return std::make_pair(channels, device["channels"]);
    }

    Json::Value customChannels(Json::arrayValue);
    std::unordered_map<std::string, Json::Value> regs;
    if (device.isMember("channels")) {
        for (Json::Value& channel: device["channels"]) {
            regs[channel["name"].asString()] = channel;
        }
    }

    for (const Json::Value& ch: templates.GetTemplate(device["device_type"].asString())["channels"]) {
        auto it = regs.find(ch["name"].asString());
        if (it != regs.end()) {
            channels.append(RemoveReadonlyProperties(it->second));
            regs.erase(it);
        } else {
            channels.append(RemoveReadonlyProperties(ch));
        }
    }

    for (auto ch: regs) {
        customChannels.append(ch.second);
    }

    return std::make_pair(channels, customChannels);
}

Json::Value FilterStandardChannels(Json::Value& device, TTemplateMap& templates)
{
    Json::Value channels(Json::arrayValue);
    if (!device.isMember("device_type")) {
        return channels;
    }

    std::unordered_map<std::string, Json::Value> regs;
    if (device.isMember("standard_channels")) {
        for (const Json::Value& channel: templates.GetTemplate(device["device_type"].asString())["channels"]) {
            regs[channel["name"].asString()] = channel;
        }
    }

    for (Json::Value& ch: device["standard_channels"]) {
        auto it = regs.find(ch["name"].asString());
        if (it != regs.end()) {
            bool hasPollInterval = false;
            if (ch.isMember("poll_interval")) {
                hasPollInterval = true;
                int pollIntervalToErase = DefaultPollInterval.count();
                if (it->second.isMember("poll_interval")) {
                    pollIntervalToErase = it->second["poll_interval"].asInt();
                }
                if (ch["poll_interval"].asInt() == pollIntervalToErase) {
                    ch.removeMember("poll_interval");
                    hasPollInterval = false;
                }
            }
            if (ch.isMember("enabled") && (!ch["enabled"].asBool() || hasPollInterval)) {
                channels.append(ch);
            }
        }
    }

    return channels;
}

std::string GetHashedParam(const std::string& deviceType, const std::string& prefix)
{
    return prefix + "_" + std::to_string(std::hash<std::string>()(deviceType));
}

Json::Value MakeJsonForConfed(const std::string& configFileName, const Json::Value& configSchema, TTemplateMap& templates)
{
    Json::Value root(Parse(configFileName));
    Validate(root, configSchema);
    for (Json::Value& port : root["ports"]) {
        for (Json::Value& device : port["devices"]) {
            Json::Value customChannels;
            Json::Value standardChannels;
            std::tie(standardChannels, customChannels) = SplitChannels(device, templates);
            if (device.isMember("device_type")) {
                Json::Value newDev;
                auto dt = device["device_type"].asString();
                newDev["device_type"] = dt;
                device.removeMember("device_type");
                newDev["slave_id"] = device["slave_id"];
                device.removeMember("slave_id");
                if (device.isMember("name")) {
                    newDev["name"] = device["name"];
                    device.removeMember("name");
                }
                if ( customChannels.size() ) {
                    device["channels"] = customChannels;
                } else {
                    device.removeMember("channels");
                }
                if ( standardChannels.size() ) {
                    device["standard_channels"] = standardChannels;
                }
                newDev[GetHashedParam(dt, "s")] = device;
                device = newDev;
            }
        }
    }

    return root;
}

Json::Value MakeConfigFromConfed(std::istream& stream, TTemplateMap& templates)
{
    Json::Value  root;
    Json::Reader reader;

    if (!reader.parse(stream, root, false)) {
        throw std::runtime_error("Failed to parse JSON:" + reader.getFormattedErrorMessages());
    }

    for (Json::Value& port : root["ports"]) {
        for (Json::Value& device : port["devices"]) {
            if (device.isMember("device_type")) {
                auto sName = GetHashedParam(device["device_type"].asString(), "s");
                auto& s = device[sName];
                for (Json::Value::iterator it = s.begin(); it != s.end(); ++it) {
                    device[it.name()] = s[it.name()];
                }
                device.removeMember(sName);
            }
            Json::Value filteredChannels = FilterStandardChannels(device, templates);
            device.removeMember("standard_channels");
            if ( device.isMember("channels") ) {
                for (Json::Value& ch : filteredChannels) {
                    device["channels"].append(ch);
                }
            } else {
                device["channels"] = filteredChannels;
            }
            if (device["channels"].empty()) {
                device.removeMember("channels");
            }
        }
    }

    return root;
}


//  {
//      "type": "object",
//      "title": DEVICE_TYPE,
//      "options": {
//          "disable_edit_json": true
//      },
//      "required": ["device_type", "slave_id"],
//      "defaultProperties": ["device_type", "slave_id"],
//      "properties": {
//          "slave_id": {
//              "title": "Slave id",
//              "$ref": "#/definitions/slave_id"
//          },
//          "device_name" : {
//              "$ref" : "#/definitions/device_name",
//              "title" : "Device name"
//          }
//          "device_type": {
//              "type": "string",
//              "enum": [ DEVICE_TYPE ],
//              "options": {
//                  "hidden": true
//              }
//          },
//          "s_DEVICE_HASH": {
//              "title": "Settings",
//              "allOf": [
//                  { "$ref": "#/definitions/deviceCommon" },
//              ],
//              "properties": {
//                  "setup": SETUP_SCHEMA,
//                  "standard_channels": {
//                      "type": "array",
//                      "_format": "table",
//                      "title": "List of standard channels",
//                      "items": { "$ref": "#/definitions/channelSettings" },
//                      "default": [
//                          {
//                              "name": CHANNEL_NAME,
//                              "poll_interval": POLL_INTERVAL
//                          },
//                          ....
//                      ],
//                      "options": {
//                          "disable_array_delete": true,
//                          "disable_array_reorder": true,
//                          "disable_array_add": true,
//                          "collapsed": true
//                      },
//                      "propertyOrder": 9
//                  }
//              },
//              "options": {
//                  "disable_edit_json": true
//              },
//              "defaultProperties": ["enabled"]
//          }
//      }
//  }
Json::Value MakeDeviceUISchema(const std::string& deviceType, const Json::Value& t)
{
    Json::Value res;
    res["type"] = "object";
    res["title"] = deviceType;
    res["options"]["disable_edit_json"] = true;

    Json::Value req(Json::arrayValue);
    req.append("device_type");
    req.append("slave_id");
    res["required"] = req;
    res["defaultProperties"] = req;

    res["properties"]["slave_id"]["title"] = "Slave id";
    res["properties"]["slave_id"]["$ref"] = "#/definitions/slave_id";

    res["properties"]["device_name"]["title"] = "Device name";
    res["properties"]["device_name"]["$ref"] = "#/definitions/device_name";

    res["properties"]["device_type"]["type"] = "string";
    res["properties"]["device_type"]["options"]["hidden"] = true;
    req.clear();
    req.append(deviceType);
    res["properties"]["device_type"]["enum"] = req;

    Json::Value ar(Json::arrayValue);
    Json::Value ref;
    ref["$ref"] = "#/definitions/deviceCommon";
    ar.append(ref);

    auto set = GetHashedParam(deviceType, "s");
    if (ar.size()) {
        res["properties"][set]["allOf"] = ar;
    }
    res["properties"][set]["title"] = "Settings";
    res["properties"][set]["type"] = "object";
    res["properties"][set]["options"]["disable_edit_json"] = true;
    ar.clear();
    ar.append("enabled");
    res["properties"][set]["defaultProperties"] = ar;

    const auto& channels = t["channels"];
    Json::Value def(Json::arrayValue);
    for (const auto& ch: channels) {
        Json::Value v;
        v["name"] = ch["name"];
        if (ch.isMember("poll_interval")) {
            v["poll_interval"] = ch["poll_interval"];
        } else {
            v["poll_interval"] = 20;
        }
        if (ch.isMember("enabled")) {
            v["enabled"] = ch["enabled"];
        } else {
            v["enabled"] = true;
        }
        def.append(v);
    }
    res["properties"][set]["properties"]["standard_channels"]["default"] = def;
    res["properties"][set]["properties"]["standard_channels"]["type"] = "array";
    res["properties"][set]["properties"]["standard_channels"]["_format"] = "table";
    res["properties"][set]["properties"]["standard_channels"]["title"] = "List of standard channels";
    res["properties"][set]["properties"]["standard_channels"]["propertyOrder"] = 9;
    res["properties"][set]["properties"]["standard_channels"]["items"]["$ref"] = "#/definitions/channelSettings";
    res["properties"][set]["properties"]["standard_channels"]["options"]["disable_array_delete"] = true;
    res["properties"][set]["properties"]["standard_channels"]["options"]["disable_array_reorder"] = true;
    res["properties"][set]["properties"]["standard_channels"]["options"]["disable_array_add"] = true;
    res["properties"][set]["properties"]["standard_channels"]["options"]["collapsed"] = true;

    if (t.isMember("setup_schema")) {
        res["properties"][set]["properties"]["setup"] = t["setup_schema"];
    }

    return res;
}

void AppendDeviceSchemas(Json::Value& list, TTemplateMap& templates)
{
    for (const auto& t: templates.GetTemplates()) {
        try {
            list.append(MakeDeviceUISchema(t.first, t.second));
        } catch (const std::exception& e) {
            LOG(Error) << e.what();
        }
    }
}

void MakeSchemaForConfed(Json::Value& configSchema, TTemplateMap& templates)
{
    // Let's replace #/definitions/device/oneOf typedCustomDevice node by list of device's generated from templates
    if (configSchema["definitions"]["device"].isMember("oneOf")) {
        const Json::Value& array = configSchema["definitions"]["device"]["oneOf"];
        Json::Value newArray(Json::arrayValue);
        for(Json::Value::ArrayIndex index = 0; index < array.size(); ++index) {
            if (array[index].isMember("$ref") && array[index]["$ref"].asString() == "#/definitions/typedCustomDevice") {
                AppendDeviceSchemas(newArray, templates);
            } else {
                newArray.append(array[index]);
            }
        }
        configSchema["definitions"]["device"]["oneOf"] = newArray;
    }
}