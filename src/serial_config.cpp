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

#include "config_merge_template.h"

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

    int CalcRegisterAddress(int base_address, int offset, int stride, RegisterFormat format, size_t address_byte_step = 2) 
    {
        auto stride_offset = stride * RegisterFormatByteWidth(format);
        if (address_byte_step < 1) {
            address_byte_step = 1;
        }
        stride_offset = (stride_offset + address_byte_step - 1) / address_byte_step;
        return base_address + offset + stride_offset;
    }

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

    PRegisterConfig LoadRegisterConfig(PDeviceConfig      device_config,
                                       const Json::Value& register_data,
                                       std::string&       default_type_str,
                                       const std::string& channel_name,
                                       size_t             device_base_address,
                                       size_t             stride)
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

        address = CalcRegisterAddress(device_base_address, address, stride, format);

        EWordOrder word_order = register_data.isMember("word_order") ?
            WordOrderFromName(register_data["word_order"].asString()) :
            it->second.DefaultWordOrder;

        double scale    = Read(register_data, "scale",    1.0); // TBD: check for zero, too
        double offset   = Read(register_data, "offset",   0.0);
        double round_to = Read(register_data, "round_to", 0.0);

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

    void LoadSimpleChannel(PDeviceConfig      device_config,
                           const Json::Value& channel_data,
                           size_t             device_base_address,
                           size_t             stride,
                           const std::string& name_prefix,
                           const std::string& mqtt_prefix)
    {
        std::string name(channel_data["name"].asString());
        std::string mqtt_channel_name(name);
        Get(channel_data, "id", mqtt_channel_name);
        if (!mqtt_prefix.empty()) {
            mqtt_channel_name = mqtt_prefix + " " + mqtt_channel_name;
        }
        if (!name_prefix.empty()) {
            name = name_prefix + " " + name;
        }
        std::string default_type_str;
        std::vector<PRegisterConfig> registers;
        if (channel_data.isMember("consists_of")) {

            std::chrono::milliseconds poll_interval(Read(channel_data, "poll_interval", std::chrono::milliseconds(-1)));

            const Json::Value& reg_data = channel_data["consists_of"];
            for(Json::ArrayIndex i = 0; i < reg_data.size(); ++i) {
                std::string def_type;
                auto reg = LoadRegisterConfig(device_config, reg_data[i], def_type, mqtt_channel_name, device_base_address, stride);
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
            registers.push_back(LoadRegisterConfig(device_config, channel_data, default_type_str, mqtt_channel_name, device_base_address, stride));
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
                                                on_value, max, registers[0]->ReadOnly, mqtt_channel_name,
                                                registers));
        device_config->AddChannel(channel);
    }

    void LoadChannel(PDeviceConfig      device_config,
                     const Json::Value& channel_data,
                     size_t             device_base_address = 0,
                     size_t             stride              = 0,
                     const std::string& name_prefix         = "",
                     const std::string& mqtt_prefix         = "");

    void LoadSetupItem(PDeviceConfig      device_config,
                       const Json::Value& item_data,
                       size_t             device_base_address,
                       size_t             stride,
                       const std::string& mqtt_prefix);

    void LoadSubdeviceChannel(PDeviceConfig      device_config,
                              const Json::Value& channel_data,
                              size_t             device_base_address,
                              const std::string& name_prefix,
                              const std::string& mqtt_prefix)
    {
        std::string new_name_prefix(channel_data["name"].asString());
        if (!name_prefix.empty()) {
            new_name_prefix = name_prefix + " " + new_name_prefix;
        }

        std::string mqtt_channel_prefix(channel_data["name"].asString());
        Get(channel_data, "id", mqtt_channel_prefix);
        if (!mqtt_prefix.empty()) {
            if (mqtt_channel_prefix.empty()) {
                mqtt_channel_prefix = mqtt_prefix;
            } else {
                mqtt_channel_prefix = mqtt_prefix + " " + mqtt_channel_prefix;
            }
        }

        size_t baseAddress = device_base_address;
        if (channel_data.isMember("shift")) {
            baseAddress += GetInt(channel_data, "shift");
        }
        size_t stride = Read(channel_data, "stride", 0);
        if (channel_data.isMember("setup")) {
            for(const auto& setupItem: channel_data["setup"])
                LoadSetupItem(device_config, setupItem, baseAddress, stride, new_name_prefix);
        }


        if (channel_data.isMember("channels")) {
            for (const auto& ch: channel_data["channels"]) {
                LoadChannel(device_config, ch, baseAddress, stride, new_name_prefix, mqtt_channel_prefix);
            }
        }
    }

    void LoadChannel(PDeviceConfig      device_config,
                     const Json::Value& channel_data,
                     size_t             device_base_address,
                     size_t             stride,
                     const std::string& name_prefix,
                     const std::string& mqtt_prefix)
    {
        if (channel_data.isMember("enabled") && !channel_data["enabled"].asBool()) {
            return;
        }

        if (channel_data.isMember("device_type")) {
            LoadSubdeviceChannel(device_config, channel_data, device_base_address, name_prefix, mqtt_prefix);
        } else {
            LoadSimpleChannel(device_config, channel_data, device_base_address, stride, name_prefix, mqtt_prefix);
        }
    }

    void LoadSetupItem(PDeviceConfig      device_config,
                       const Json::Value& item_data,
                       size_t             device_base_address,
                       size_t             stride,
                       const std::string& name_prefix)
    {
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
        if (item_data.isMember("format")) {
            format = RegisterFormatFromName(item_data["format"].asString());
        }

        int address = CalcRegisterAddress(device_base_address, GetInt(item_data, "address"), stride, format);

        PRegisterConfig reg = TRegisterConfig::Create(
            type, address, format, 1, 0, 0, true, true, "<unspec>");

        int value = GetInt(item_data, "value");
        std::string name(Read(item_data, "title", std::string("<unnamed>")));
        if (!name_prefix.empty()) {
            name = name_prefix + " " + name;
        }
        device_config->AddSetupItem(PDeviceSetupItemConfig(new TDeviceSetupItemConfig(name, reg, value)));
    }

    void LoadDeviceTemplatableConfigPart(PDeviceConfig device_config, const Json::Value& device_data, TSerialDeviceFactory& deviceFactory, bool modbusTcpWorkaround)
    {
        device_config->Protocol = GetProtocolName(device_data);

        if (modbusTcpWorkaround) {
            if (deviceFactory.GetProtocol(device_config->Protocol)->IsModbus()) {
                device_config->Protocol += "-tcp";
            } else {
                throw TConfigParserException("Device \"" + device_config->Name + "\": protocol \"" + device_config->Protocol + "\" is not compatible with Modbus TCP");
            }
        }

        device_config->TypeMap = deviceFactory.GetRegisterTypes(device_config);

        if (device_data.isMember("setup")) {
            for(const auto& setupItem: device_data["setup"])
                LoadSetupItem(device_config, setupItem, 0, 0, "");
        }

        if (device_data.isMember("password")) {
            device_config->Password.clear();
            for(const auto& passwordItem: device_data["password"])
                device_config->Password.push_back(ToInt(passwordItem, "password item"));
        }

        if (device_data.isMember("delay_ms")) {
            LOG(Warn) << "\"delay_ms\" is not supported, use \"frame_timeout_ms\" instead";
        }

        Get(device_data, "frame_timeout_ms", device_config->FrameTimeout);
        if (device_config->FrameTimeout.count() < 0) {
            device_config->FrameTimeout = DefaultFrameTimeout;
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
    }

    void LoadDevice(PPortConfig port_config,
                    const Json::Value& device_data,
                    const std::string& default_id,
                    TTemplateMap& templates,
                    TSerialDeviceFactory& deviceFactory)
    {
        if (device_data.isMember("enabled") && !device_data["enabled"].asBool())
            return;

        auto dev = MergeDeviceConfigWithTemplate(device_data, templates);

        PDeviceConfig device_config = std::make_shared<TDeviceConfig>();
        device_config->Id = Read(dev, "id",  default_id);
        Get(dev, "device_type", device_config->DeviceType);
        Get(dev, "name",        device_config->Name);

        if (dev.isMember("slave_id")) {
            if (dev["slave_id"].isString())
                device_config->SlaveId = dev["slave_id"].asString();
            else // legacy
                device_config->SlaveId = std::to_string(dev["slave_id"].asInt());
        }

        LoadDeviceTemplatableConfigPart(device_config, dev, deviceFactory, port_config->IsModbusTcp);

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

        auto device_poll_interval = port_config->PollInterval;
        Get(device_data, "poll_interval", device_poll_interval);
        for (auto channel: device_config->DeviceChannelConfigs) {
            for (auto reg: channel->RegisterConfigs) {
                if (reg->PollInterval.count() < 0) {
                    reg->PollInterval = device_poll_interval;
                }
            }
        }

        port_config->AddDeviceConfig(device_config, deviceFactory);
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

Json::Value TTemplateMap::Validate(const std::string& deviceType, const std::string& filePath)
{
    Json::Value root(WBMQTT::JSON::Parse(filePath));
    try {
        Validator->Validate(root);
    } catch (const std::runtime_error& e) {
        throw std::runtime_error("File: " + filePath + " error: " + e.what());
    }
    //Check that channels refer to valid subdevices
    if (root["device"].isMember("subdevices")) {
        TSubDevicesTemplateMap subdevices(deviceType, root["device"]);
        for (const auto& ch: root["device"]["channels"]) {
            if (ch.isMember("device_type")) {
                subdevices.GetTemplate(ch["device_type"].asString());
            }
            if (ch.isMember("oneOf")) {
                for (const auto& subdeviceType: ch["oneOf"]) {
                    subdevices.GetTemplate(subdeviceType.asString());
                }
            }
        }
    }
    return root;
}

std::shared_ptr<TDeviceTemplate> TTemplateMap::GetTemplatePtr(const std::string& deviceType) 
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
            throw std::runtime_error("Can't find template for '" + deviceType + "'");
        }
        Json::Value root(Validate(deviceType, filePath));
        TemplateFiles.erase(filePath);
        auto deviceTypeTitle = deviceType;
        Get(root, "title", deviceTypeTitle);
        auto deviceTemplate = std::make_shared<TDeviceTemplate>(deviceType, deviceTypeTitle, root["device"]);
        ValidTemplates.insert({deviceType, deviceTemplate});
        return deviceTemplate;
    }
}

const TDeviceTemplate& TTemplateMap::GetTemplate(const std::string& deviceType) 
{
    return *GetTemplatePtr(deviceType);
}

std::vector<std::shared_ptr<TDeviceTemplate>> TTemplateMap::GetTemplatesOrderedByName()
{
    std::vector<std::shared_ptr<TDeviceTemplate>> templates;
    for (const auto& file: TemplateFiles) {
        templates.push_back(GetTemplatePtr(file.first));
    }
    std::sort(templates.begin(), templates.end(), [](auto p1, auto p2) {return p1->Title < p2->Title;});
    return templates;
}

std::vector<std::string> TTemplateMap::GetDeviceTypes() const
{
    std::vector<std::string> res;
    for (const auto& elem: TemplateFiles) {
        res.push_back(elem.first);
    }
    return res;
}

TSubDevicesTemplateMap::TSubDevicesTemplateMap(const std::string& deviceType, const Json::Value& device)
    : DeviceType(deviceType)
{
    if (device.isMember("subdevices")) {
        for (auto& dev: device["subdevices"]) {
            auto deviceType = dev["device_type"].asString();
            if (Templates.count(deviceType)) {
                LOG(Warn) << "Device type '" << DeviceType << "'. Duplicate subdevice type '" << deviceType << "'";
            } else {
                auto deviceTypeTitle = deviceType;
                Get(dev, "title", deviceTypeTitle);
                Templates.insert({deviceType, {deviceType, deviceTypeTitle, dev["device"]}});
            }
        }

        //Check that channels refer to valid subdevices
        for (const auto& subdeviceTemplate: Templates) {
            for (const auto& ch: subdeviceTemplate.second.Schema["channels"]) {
                if (ch.isMember("device_type")) {
                    GetTemplate(ch["device_type"].asString());
                }
                if (ch.isMember("oneOf")) {
                    for (const auto& subdeviceType: ch["oneOf"]) {
                        GetTemplate(subdeviceType.asString());
                    }
                }
            }
        }
    }
}

const TDeviceTemplate& TSubDevicesTemplateMap::GetTemplate(const std::string& deviceType)
{
    try {
        return Templates.at(deviceType);
    } catch (...) {
        throw std::runtime_error("Device type '" + DeviceType + "'. Can't find template for subdevice '" + deviceType + "'");
    }
}

std::vector<std::string> TSubDevicesTemplateMap::GetDeviceTypes() const
{
    std::vector<std::string> res;
    for (const auto& elem: Templates) {
        res.push_back(elem.first);
    }
    return res;
}

Json::Value LoadConfigTemplatesSchema(const std::string& templateSchemaFileName, const Json::Value& configSchema)
{
    Json::Value schema = WBMQTT::JSON::Parse(templateSchemaFileName);
    AppendParams(schema["definitions"], configSchema["definitions"]);
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

    int32_t maxUnchangedInterval = -1;
    Get(Root, "max_unchanged_interval", maxUnchangedInterval);
    handlerConfig->PublishParameters.Set(maxUnchangedInterval);

    const Json::Value& array = Root["ports"];
    for(Json::Value::ArrayIndex index = 0; index < array.size(); ++index) {
        // old default prefix for compat
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
                                           int                max,
                                           bool               readOnly,
                                           const std::string& mqttId,
                                           const std::vector<PRegisterConfig> regs)
    : Name(name), MqttId(mqttId), Type(type), DeviceId(deviceId),
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
    auto addrIt = SetupItemsByAddress.find(item->RegisterConfig->Address);
    if (addrIt != SetupItemsByAddress.end()) {
        LOG(Warn) << "Setup command \"" << item->Name << "\" will be ignored. It has the same address " 
                  << item->RegisterConfig->Address << " as command \"" << addrIt->second << "\"";
    } else {
        SetupItemsByAddress.insert({item->RegisterConfig->Address, item->Name});
        SetupItemConfigs.push_back(item);
    }
}

std::string TDeviceConfig::GetDescription() const
{
    return "Device " + Name + " " + Id 
                     + DecorateIfNotEmpty(" (", DeviceType, ")")
                     + DecorateIfNotEmpty(", protocol: ", Protocol);
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
        dst[it.name()] = src[it.name()];
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

TDeviceTemplate::TDeviceTemplate(const std::string& type, const std::string title, const Json::Value& schema)
    : Type(type), Title(title), Schema(schema)
{}
