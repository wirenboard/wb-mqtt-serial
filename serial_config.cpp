#include <iostream>
#include <fstream>
#include <stdexcept>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string>

#include "serial_config.h"

namespace {
    const char* DefaultProtocol = "modbus";
}

TConfigTemplateParser::TConfigTemplateParser(const std::string& template_config_dir, bool debug)
    : DirectoryName(template_config_dir),
      Debug(debug) {}

TTemplateMap TConfigTemplateParser::Parse()
{
    DIR *dir;
    struct dirent *dirp;
    struct stat filestat;

    if ((dir = opendir(DirectoryName.c_str())) == NULL)
        throw TConfigParserException("Cannot open templates directory");

    while ((dirp = readdir(dir))) {
        std::string dname = dirp->d_name;
        if(dname == "." || dname == "..")
            continue;

        std::string filepath = DirectoryName + "/" + dname;
        if (stat(filepath.c_str(), &filestat)) continue;
        if (S_ISDIR(filestat.st_mode)) continue;

        std::ifstream input_stream;
        input_stream.open(filepath);
        if (!input_stream.is_open()) {
            std::cerr << "Error while trying to open template config file " << filepath << std::endl;
            continue;
        }

        Json::Reader reader;
        Json::Value root;
        if (!reader.parse(input_stream, root, false)) {
            std::cerr << "Failed to parse JSON: " << filepath << " " << reader.getFormatedErrorMessages()
                      << std::endl;
            continue;
        }

        LoadDeviceTemplate(root, filepath);
    }

    closedir(dir);
    return Templates;
}

void TConfigTemplateParser::LoadDeviceTemplate(const Json::Value& root, const std::string& filepath)
{
    if (!root.isObject())
        throw TConfigParserException("malformed config in file " + filepath);

    if (root.isMember("device_type"))
        Templates[root["device_type"].asString()] = root["device"];
    else if (Debug)
        std::cerr << "there is no device_type in json template in file " << filepath << std::endl;
}

TConfigParser::TConfigParser(const std::string& config_fname, bool force_debug,
                             TGetRegisterTypeMap get_register_type_map,
                             TTemplateMap templates)
    : ConfigFileName(config_fname),
      HandlerConfig(new THandlerConfig),
      GetRegisterTypeMap(get_register_type_map),
      Templates(templates)
{
    HandlerConfig->Debug = force_debug;
}

PRegister TConfigParser::LoadRegister(PDeviceConfig device_config,
                                      const Json::Value& register_data,
                                      std::string& default_type_str)
{
    int address = GetInt(register_data, "address");
    std::string reg_type_str = register_data["reg_type"].asString();
    default_type_str = "text";
    auto it = device_config->TypeMap->find(reg_type_str);
    if (it == device_config->TypeMap->end())
        throw TConfigParserException("invalid register type: " + reg_type_str + " -- " + device_config->DeviceType);
    if (!it->second.DefaultControlType.empty())
        default_type_str = it->second.DefaultControlType;

    RegisterFormat format = register_data.isMember("format") ?
        RegisterFormatFromName(register_data["format"].asString()) :
        it->second.DefaultFormat;

    double scale = 1;
    if (register_data.isMember("scale"))
        scale = register_data["scale"].asDouble(); // TBD: check for zero, too

    bool force_readonly = false;
    if (register_data.isMember("readonly"))
        force_readonly = register_data["readonly"].asBool();

    return std::make_shared<TRegister>(
        device_config->SlaveId, it->second.Index,
        address, format, scale, true, force_readonly || it->second.ReadOnly,
        it->second.Name);
}

void TConfigParser::LoadChannel(PDeviceConfig device_config, const Json::Value& channel_data)
{
    if (!channel_data.isObject())
        throw TConfigParserException("malformed config -- " + device_config->DeviceType);

    std::string name = channel_data["name"].asString();
    std::string default_type_str;
    std::vector<PRegister> registers;
    if (channel_data.isMember("consists_of")) {
        const Json::Value array = channel_data["consists_of"];
        for(unsigned int index = 0; index < array.size(); ++index) {
            std::string def_type;
            registers.push_back(LoadRegister(device_config, array[index], def_type));
            if (!index)
                default_type_str = def_type;
            else if (registers[index]->ReadOnly != registers[0]->ReadOnly)
                throw TConfigParserException(("can't mix read-only and writable registers "
                                              "in one channel -- ") + device_config->DeviceType);
        }
        if (!registers.size())
            throw TConfigParserException("empty \"consists_of\" section -- " + device_config->DeviceType);
    } else
        registers.push_back(LoadRegister(device_config, channel_data, default_type_str));

    std::string type_str = channel_data["type"].asString();
    if (type_str.empty())
        type_str = default_type_str;
    if (type_str == "wo-switch") {
        type_str = "switch";
        for (auto& reg: registers)
            reg->Poll = false;
    }

    std::string on_value = "";
    if (channel_data.isMember("on_value")) {
        if (registers.size() != 1)
            throw TConfigParserException("can only use on_value for single-valued controls -- " +
                                         device_config->DeviceType);
        on_value = std::to_string(GetInt(channel_data, "on_value"));
    }

    int max = -1;
    if (channel_data.isMember("max"))
        max = GetInt(channel_data, "max");

    int order = device_config->NextOrderValue();
    PDeviceChannel channel(new TDeviceChannel(name, type_str, device_config->Id, order,
                                              on_value, max, registers[0]->ReadOnly,
                                              registers));
    device_config->AddChannel(channel);
}

void TConfigParser::LoadSetupItem(PDeviceConfig device_config, const Json::Value& item_data)
{
    if (!item_data.isObject())
        throw TConfigParserException("malformed config -- " + device_config->DeviceType);

    std::string name = item_data.isMember("title") ?
        item_data["title"].asString() : "<unnamed>";
    if (!item_data.isMember("address"))
        throw TConfigParserException("no address specified for init item");

    int address = GetInt(item_data, "address");
    std::string reg_type_str = item_data["reg_type"].asString();
    int type = 0;
    std::string type_name = "<unspec>";
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
    PRegister reg = std::make_shared<TRegister>(
        device_config->SlaveId, type,
        address, format, 1, true, true, type_name);

    if (!item_data.isMember("value"))
        throw TConfigParserException("no reg specified for init item");
    int value = GetInt(item_data, "value");
    device_config->AddSetupItem(PDeviceSetupItem(new TDeviceSetupItem(name, reg, value)));
}

void TConfigParser::LoadDeviceVectors(PDeviceConfig device_config, const Json::Value& device_data)
{
    if (device_data.isMember("protocol"))
        device_config->Protocol = device_data["protocol"].asString();
    else if (device_config->Protocol.empty())
        device_config->Protocol = DefaultProtocol;
    device_config->TypeMap = GetRegisterTypeMap(device_config);

    if (device_data.isMember("setup")) {
        const Json::Value array = device_data["setup"];
        for(unsigned int index = 0; index < array.size(); ++index)
            LoadSetupItem(device_config, array[index]);
    }

    if (device_data.isMember("password")) {
        const Json::Value array = device_data["password"];
        device_config->Password.clear();
        for(unsigned int index = 0; index < array.size(); ++index)
            device_config->Password.push_back(ToInt(array[index], "password item"));
    }

    if (device_data.isMember("delay_usec")) // compat
        device_config->DelayUSec = GetInt(device_data, "delay_usec");
    else if (device_data.isMember("delay_ms"))
        device_config->DelayUSec = GetInt(device_data, "delay_ms") * 1000;

    if (device_data.isMember("frame_timeout_ms"))
        device_config->FrameTimeout = GetInt(device_data, "frame_timeout_ms");

    const Json::Value array = device_data["channels"];
    for(unsigned int index = 0; index < array.size(); ++index)
        LoadChannel(device_config, array[index]);
}

int TConfigParser::ToInt(const Json::Value& v, const std::string& title)
{
    if (v.isInt())
        return v.asInt();

    if (v.isString()) {
        try {
            return std::stoi(v.asString(), /*pos= */ 0, /*base= */ 0);
        } catch (const std::logic_error& e) {}
    }

    throw TConfigParserException(title + ": plain integer or '0x..' hex string expected instead of " + v.asString());
    // v.asString() should give a bit more information what config this exception came from
}

int TConfigParser::GetInt(const Json::Value& obj, const std::string& key)
{
    return ToInt(obj[key], key);
}

PHandlerConfig TConfigParser::Parse()
{
    Json::Reader reader;
    if (ConfigFileName.empty())
        throw TConfigParserException("Please specify config file with -c option");

    std::ifstream myfile (ConfigFileName);
    if (myfile.fail())
        throw TConfigParserException("Serial driver configuration file not found: " + ConfigFileName);

    if(!reader.parse(myfile, Root, false))
        throw TConfigParserException("Failed to parse JSON: " + reader.getFormatedErrorMessages());

    LoadConfig();
    return HandlerConfig;
}

void TConfigParser::LoadDevice(PPortConfig port_config,
                               const Json::Value& device_data,
                               const std::string& default_id)
{
    if (!device_data.isObject())
        throw TConfigParserException("malformed config");
    if (device_data.isMember("enabled") && !device_data["enabled"].asBool())
        return;

    PDeviceConfig device_config(new TDeviceConfig);
    device_config->Id = device_data.isMember("id") ? device_data["id"].asString() : default_id;
    device_config->Name = device_data.isMember("name") ? device_data["name"].asString() : "";
    device_config->SlaveId = GetInt(device_data, "slave_id");
    if (device_data.isMember("device_type")){
        device_config->DeviceType = device_data["device_type"].asString();
        auto it = Templates.find(device_config->DeviceType);
        if (it != Templates.end()) {
            if (it->second.isMember("name")) {
                if (device_config->Name == "")
                    device_config->Name = it->second["name"].asString() + " " +
                        std::to_string(device_config->SlaveId);
            } else if (device_config->Name == "")
                    throw TConfigParserException(
                        "Property device_name is missing in " + device_config->DeviceType + " template");

            if (it->second.isMember("id")) {
                if (device_config->Id == default_id)
                    device_config->Id = it->second["id"].asString() + "_" +
                        std::to_string(device_config->SlaveId);
            }

            LoadDeviceVectors(device_config, it->second);
        } else // XXX should probably throw TConfigParserException here?
            std::cerr << "Can't find the template for '"
                      << device_config->DeviceType
                      << "' device type." << std::endl;
    }
    LoadDeviceVectors(device_config, device_data);
    port_config->AddDeviceConfig(device_config);
}

void TConfigParser::LoadPort(const Json::Value& port_data,
                             const std::string& id_prefix)
{
    if (!port_data.isObject())
        throw TConfigParserException("malformed config");

    if (!port_data.isMember("path"))
        throw TConfigParserException("path not specified");

    if (port_data.isMember("enabled") && !port_data["enabled"].asBool())
        return;

    PPortConfig port_config(new TPortConfig);
    port_config->ConnSettings.Device = port_data["path"].asString();

    if (port_data.isMember("baud_rate"))
        port_config->ConnSettings.BaudRate = GetInt(port_data, "baud_rate");

    if (port_data.isMember("parity"))
        port_config->ConnSettings.Parity = port_data["parity"].asCString()[0]; // FIXME (can be '\0')

    if (port_data.isMember("data_bits"))
        port_config->ConnSettings.DataBits = GetInt(port_data, "data_bits");

    if (port_data.isMember("stop_bits"))
        port_config->ConnSettings.StopBits = GetInt(port_data, "stop_bits");

    if (port_data.isMember("response_timeout_ms"))
        port_config->ConnSettings.ResponseTimeoutMs = GetInt(port_data, "response_timeout_ms");

    if (port_data.isMember("poll_interval"))
        port_config->PollInterval = GetInt(port_data, "poll_interval");

    const Json::Value array = port_data["devices"];
    for(unsigned int index = 0; index < array.size(); ++index)
            LoadDevice(port_config, array[index], id_prefix + std::to_string(index));

    HandlerConfig->AddPortConfig(port_config);
}

void TConfigParser::LoadConfig()
{
    if (!Root.isObject())
        throw TConfigParserException("malformed config");

    if (!Root.isMember("ports"))
        throw TConfigParserException("no ports specified");

    if (Root.isMember("debug"))
        HandlerConfig->Debug = HandlerConfig->Debug || Root["debug"].asBool();

    const Json::Value array = Root["ports"];
    for(unsigned int index = 0; index < array.size(); ++index)
        LoadPort(array[index], "wb-modbus-" + std::to_string(index) + "-"); // XXX old default prefix for compat

    // check are there any devices defined
    for (const auto& port_config : HandlerConfig->PortConfigs) {
        if (!port_config->DeviceConfigs.empty()) { // found one
            return;
        }
    }

    throw TConfigParserException("no devices defined in config. Nothing to do");
}
