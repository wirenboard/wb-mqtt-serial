#include <iostream>
#include <fstream>
#include <stdexcept>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <string>

#include "modbus_config.h"
#include "wbmqtt/utils.h"

using namespace std;

namespace {
    const char* DefaultProtocol = "modbus";
}

TConfigTemplateParser::TConfigTemplateParser(const string& template_config_dir, bool debug)
    : DirectoryName(template_config_dir),
      Debug(debug)
{
}

map<string, TDeviceJson> TConfigTemplateParser::Parse()
{
    DIR *dir;
    struct dirent *dirp;
    struct stat filestat;
    if ((dir = opendir(DirectoryName.c_str())) == NULL) {
        cerr << "Cannot open templates directory";
        exit(EXIT_FAILURE);
    }
    while ((dirp = readdir(dir))) {
        string dname = dirp->d_name;
        if(dname == "." || dname == "..")
            continue;
        string filepath = DirectoryName + "/" + dname;
        if (stat( filepath.c_str(), &filestat )) continue;
        if (S_ISDIR( filestat.st_mode ))         continue;

        ifstream input_stream;
        input_stream.open(filepath);
        if (!input_stream.is_open()) {
            cerr << "Error while trying to open template config file " << filepath << endl;
            continue;
        }
        Json::Reader reader;
        Json::Value root;
        bool parsedSuccess = reader.parse(input_stream, root, false);

        // Report failures and their locations in the document.
        if(not parsedSuccess) {
            cerr << "Failed to parse JSON: " << filepath << " " << reader.getFormatedErrorMessages() << endl;
            continue;
        }

        LoadDeviceTemplate(root, filepath);
    }
    closedir(dir);
    return Templates;
}

void TConfigTemplateParser::LoadDeviceTemplate(const Json::Value& root, const string& filepath)
{
    if (!root.isObject()) {
        cerr << "malformed config in file " + filepath;
        exit(EXIT_FAILURE);
    }
    if (root.isMember("device_type")) {
            Templates[root["device_type"].asString()] = root["device"];
    } else {
        if (Debug)
            cerr << "there is no device_type in json template in file " << filepath << endl;
    }
}

PModbusRegister TConfigActionParser::LoadRegister(PDeviceConfig device_config,
                                            const Json::Value& register_data,
                                            std::string& default_type_str)
{
    int address = GetInt(register_data, "address");
    std::string reg_type_str = register_data["reg_type"].asString();
    default_type_str = "text";
    TModbusRegister::RegisterType type;
    if (reg_type_str == "coil") {
        type = TModbusRegister::RegisterType::COIL;
        default_type_str = "switch";
    } else if (reg_type_str == "discrete") {
        type = TModbusRegister::RegisterType::DISCRETE_INPUT;
        default_type_str = "switch";
    } else if (reg_type_str == "holding") {
        type = TModbusRegister::RegisterType::HOLDING_REGISTER;
        default_type_str = "text";
    } else if (reg_type_str == "input") {
        type = TModbusRegister::RegisterType::INPUT_REGISTER;
        default_type_str = "text";
    } else if (reg_type_str == "direct") {
        type = TModbusRegister::RegisterType::DIRECT_REGISTER;
        default_type_str = "text";
    } else
        throw TConfigParserException("invalid register type: " + reg_type_str + " " + device_config->DeviceType);

    RegisterFormat format = U16;
    if (register_data.isMember("format")) {
        std::string format_str = register_data["format"].asString();
        if (format_str == "s16")
            format = S16;
        else if (format_str == "u8")
            format = U8;
        else if (format_str == "s8")
            format = S8;
        else if (format_str == "u24")
            format = U24;
        else if (format_str == "s24")
            format = S24;
        else if (format_str == "u32")
            format = U32;
        else if (format_str == "s32")
            format = S32;
        else if (format_str == "s64")
            format = S64;
        else if (format_str == "u64")
            format = U64;
        else if (format_str == "bcd8")
            format = BCD8;
        else if (format_str == "bcd16")
            format = BCD16;
        else if (format_str == "bcd24")
            format = BCD24;
        else if (format_str == "bcd32")
            format = BCD32;
        else if (format_str == "float")
            format = Float;
        else if (format_str == "double")
            format = Double;
    }

    double scale = 1;
    if (register_data.isMember("scale"))
        scale = register_data["scale"].asDouble(); // TBD: check for zero, too

    bool force_readonly = false;
    if (register_data.isMember("readonly"))
        force_readonly = register_data["readonly"].asBool();

    PModbusRegister ptr(new TModbusRegister(device_config->SlaveId, type, address, format, scale, true, force_readonly));
    return ptr;
}

void TConfigActionParser::LoadChannel(PDeviceConfig device_config, const Json::Value& channel_data)
{
    if (!channel_data.isObject())
        throw TConfigParserException(string("malformed config") + " " + device_config->DeviceType);

    std::string name = channel_data["name"].asString();
    std::string default_type_str;
    std::vector<PModbusRegister> registers;
    if (channel_data.isMember("consists_of")) {
        const Json::Value array = channel_data["consists_of"];
        for(unsigned int index = 0; index < array.size(); ++index) {
            std::string def_type;
            registers.push_back(LoadRegister(device_config, array[index], def_type));
            if (!index)
                default_type_str = def_type;
            else if (registers[index]->IsReadOnly() != registers[0]->IsReadOnly())
                throw TConfigParserException(string("can't mix read-only and writable registers "
                                             "in one channel") + " " + device_config->DeviceType);
        }
        if (!registers.size())
            throw TConfigParserException(string("empty \"consists_of\" section") + " " + device_config->DeviceType);
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
            throw TConfigParserException(string("can only use on_value for single-valued controls") + " " + device_config->DeviceType);
        on_value = to_string(GetInt(channel_data, "on_value"));
    }

    int max = -1;
    if (channel_data.isMember("max"))
        max = GetInt(channel_data, "max");

    int order = device_config->NextOrderValue();
    PModbusChannel channel(new TModbusChannel(name, type_str, device_config->Id, order,
                                              on_value, max, registers[0]->IsReadOnly(),
                                              registers));
    device_config->AddChannel(channel);
}

void TConfigActionParser::LoadSetupItem(PDeviceConfig device_config, const Json::Value& item_data)
{
    if (!item_data.isObject())
        throw TConfigParserException(string("malformed config") + " " + device_config->DeviceType);

    std::string name = item_data.isMember("title") ?
        item_data["title"].asString() : "<unnamed>";
    if (!item_data.isMember("address"))
        throw TConfigParserException("no address specified for init item");
    int address = GetInt(item_data, "address");
    if (!item_data.isMember("value"))
        throw TConfigParserException("no reg specified for init item");
    int value = GetInt(item_data, "value");
    device_config->AddSetupItem(PDeviceSetupItem(new TDeviceSetupItem(name, address, value)));
}

void TConfigActionParser::LoadDeviceVectors(PDeviceConfig device_config, const Json::Value& device_data)
{
    if (device_data.isMember("protocol"))
        device_config->Protocol = device_data["protocol"].asString();

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

    if (device_data.isMember("delay_usec"))
        device_config->DelayUSec = GetInt(device_data, "delay_usec");

    const Json::Value array = device_data["channels"];
    for(unsigned int index = 0; index < array.size(); ++index)
        LoadChannel(device_config, array[index]);
}

int TConfigActionParser::ToInt(const Json::Value& v, const std::string& title)
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

int TConfigActionParser::GetInt(const Json::Value& obj, const std::string& key)
{
    return ToInt(obj[key], key);
}

PHandlerConfig TConfigParser::Parse()
{
    // Let's parse it
    Json::Reader reader;
    if (ConfigFileName.empty())
        throw TConfigParserException("Please specify config file with -c option");

    std::ifstream myfile (ConfigFileName);
    if (myfile.fail())
        throw TConfigParserException("Modbus driver configuration file not found: " + ConfigFileName);

    bool parsedSuccess = reader.parse(myfile, root, false);

    // Report failures and their locations in the document.
    if(not parsedSuccess)
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
        std::map<string, TDeviceJson>::iterator it = TemplatesMap.find(device_config->DeviceType);
        if (it != TemplatesMap.end()){
            if (it->second.isMember("name")) {
                if (device_config->Name == "")
                    device_config->Name = it->second["name"].asString() + " " + to_string(device_config->SlaveId);
            }else {
                if (device_config->Name == "")
                    throw TConfigParserException("Property device_name is missing in " + device_config->DeviceType + " template");
            }
            if (it->second.isMember("id")) {
                if (device_config->Id == default_id)
                    device_config->Id = it->second["id"].asString() + "_" + to_string(device_config->SlaveId);
            }

            LoadDeviceVectors(device_config, it->second);
        }
        else{
            std::cerr << "Can't find the template for '"
                      << device_config->DeviceType
                      << "' device type." << std::endl;
        }
    }
    LoadDeviceVectors(device_config, device_data);
    if (device_config->Protocol.empty())
        device_config->Protocol = port_config->Protocol.empty() ? DefaultProtocol :
            port_config->Protocol;

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

    if (port_data.isMember("protocol"))
        port_config->Protocol = port_data["protocol"].asString();
    else if (port_data.isMember("type")) // compatibility
        port_config->Protocol = port_data["type"].asString();

    const Json::Value array = port_data["devices"];
    for(unsigned int index = 0; index < array.size(); ++index)
            LoadDevice(port_config, array[index], id_prefix + std::to_string(index));

    HandlerConfig->AddPortConfig(port_config);
}

void TConfigParser::LoadConfig()
{
    if (!root.isObject())
        throw TConfigParserException("malformed config");

    if (!root.isMember("ports"))
        throw TConfigParserException("no ports specified");

    if (root.isMember("debug"))
        HandlerConfig->Debug = HandlerConfig->Debug || root["debug"].asBool();

    const Json::Value array = root["ports"];
    for(unsigned int index = 0; index < array.size(); ++index)
        LoadPort(array[index], "wb-modbus-" + std::to_string(index) + "-");

    // check are there any devices defined
    for (const auto& port_config : HandlerConfig->PortConfigs) {
        if (!port_config->DeviceConfigs.empty()) { // found one
            return;
        }
    }

    throw TConfigParserException("no devices defined in config. Nothing to do");
}
