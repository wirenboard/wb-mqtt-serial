#include <iostream>
#include <fstream>

#include "modbus_config.h"

PHandlerConfig TConfigParser::parse()
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

TModbusRegister TConfigParser::LoadRegister(PDeviceConfig device_config,
                                            const Json::Value& register_data,
                                            std::string& default_type_str)
{
    int address = register_data["address"].asInt();
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
    } else
        throw TConfigParserException("invalid register type: " + reg_type_str);

    TModbusRegister::RegisterFormat format = TModbusRegister::U16;
    if (register_data.isMember("format")) {
        std::string format_str = register_data["format"].asString();
        if (format_str == "s16")
            format = TModbusRegister::S16;
        else if (format_str == "u8")
            format = TModbusRegister::U8;
        else if (format_str == "s8")
            format = TModbusRegister::S8;
    }

    double scale = 1;
    if (register_data.isMember("scale"))
        scale = register_data["scale"].asDouble(); // TBD: check for zero, too

    return TModbusRegister(device_config->SlaveId, type, address, format, scale, true);
}

void TConfigParser::LoadChannel(PDeviceConfig device_config, const Json::Value& channel_data)
{
    if (!channel_data.isObject())
        throw TConfigParserException("malformed config");

    if (channel_data.isMember("enabled") && !channel_data["enabled"].asBool())
        return;

    std::string name = channel_data["name"].asString();
    std::string default_type_str;
    std::vector<TModbusRegister> registers;
    if (channel_data.isMember("consists_of")) {
        const Json::Value array = channel_data["consists_of"];
        for(unsigned int index = 0; index < array.size(); ++index) {
            std::string def_type;
            registers.push_back(LoadRegister(device_config, array[index], def_type));
            if (!index)
                default_type_str = def_type;
            else if (registers[index].IsReadOnly() != registers[0].IsReadOnly())
                throw TConfigParserException("can't mix read-only and writable registers "
                                             "in one channel");
        }
        if (!registers.size())
            throw TConfigParserException("empty \"consists_of\" section");
    } else
        registers.push_back(LoadRegister(device_config, channel_data, default_type_str));

    std::string type_str = channel_data["type"].asString();
    if (type_str.empty())
        type_str = default_type_str;
    if (type_str == "wo-switch") {
        type_str = "switch";
        for (auto& reg: registers)
            reg.Poll = false;
    }

    int on_value = -1;
    if (channel_data.isMember("on_value")) {
        if (registers.size() != 1)
            throw TConfigParserException("can only use on_value for single-valued controls");
        on_value = channel_data["on_value"].asInt();
    }

    int max = -1;
    if (channel_data.isMember("max"))
        max = channel_data["max"].asInt();

    int order = device_config->NextOrderValue();
    PModbusChannel channel(new TModbusChannel(name, type_str, device_config->Id, order,
                                              on_value, max, registers[0].IsReadOnly(),
                                              registers));
    if (HandlerConfig->Debug)
        std::cerr << "channel " << channel->Name << " device id: " << channel->DeviceId << std::endl;
    device_config->AddChannel(channel);
}

void TConfigParser::LoadSetupItem(PDeviceConfig device_config, const Json::Value& item_data)
{
    if (!item_data.isObject())
        throw TConfigParserException("malformed config");

    std::string name = item_data.isMember("name") ?
        item_data["name"].asString() : "<unnamed>";
    if (!item_data.isMember("address"))
        throw TConfigParserException("no address specified for init item");
    int address = item_data["address"].asInt();
    if (!item_data.isMember("value"))
        throw TConfigParserException("no reg specified for init item");
    int value = item_data["value"].asInt();
    device_config->AddSetupItem(PDeviceSetupItem(new TDeviceSetupItem(name, address, value)));
}

void TConfigParser::LoadDevice(PPortConfig port_config,
                               const Json::Value& device_data,
                               const std::string& default_id)
{
    if (!device_data.isObject())
        throw TConfigParserException("malformed config");

    if (!device_data.isMember("name"))
        throw TConfigParserException("device name not specified");

    if (device_data.isMember("enabled") && !device_data["enabled"].asBool())
        return;

    PDeviceConfig device_config(new TDeviceConfig);
    device_config->Id = device_data.isMember("id") ? device_data["id"].asString() : default_id;
    device_config->Name = device_data["name"].asString();
    device_config->SlaveId = device_data["slave_id"].asInt();

    if (device_data.isMember("setup")) {
        const Json::Value array = device_data["setup"];
        for(unsigned int index = 0; index < array.size(); ++index)
            LoadSetupItem(device_config, array[index]);
    }

    const Json::Value array = device_data["channels"];
    for(unsigned int index = 0; index < array.size(); ++index)
        LoadChannel(device_config, array[index]);

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
        port_config->ConnSettings.BaudRate = port_data["baud_rate"].asInt();

    if (port_data.isMember("parity"))
        port_config->ConnSettings.DataBits = port_data["parity"].asCString()[0]; // FIXME (can be '\0')
        
    if (port_data.isMember("data_bits"))
        port_config->ConnSettings.DataBits = port_data["data_bits"].asInt();

    if (port_data.isMember("stop_bits"))
        port_config->ConnSettings.StopBits = port_data["stop_bits"].asInt();

    if (port_data.isMember("poll_interval"))
        port_config->PollInterval = port_data["poll_interval"].asInt();

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
}
