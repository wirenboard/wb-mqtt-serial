#include <iostream>
#include <fstream>

#include "modbus_config.h"

const THandlerConfig& TConfigParser::parse()
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

void TConfigParser::LoadChannel(TDeviceConfig& device_config, const Json::Value& channel_data)
{
    if (!channel_data.isObject())
        throw TConfigParserException("malformed config");

    if (channel_data.isMember("enabled") && !channel_data["enabled"].asBool())
        return;

    std::string name = channel_data["name"].asString();
    int address = channel_data["address"].asInt();
    std::string reg_type_str = channel_data["reg_type"].asString();
    std::string default_type_str = "text";
    TModbusParameter::Type type;
    if (reg_type_str == "coil") {
        type = TModbusParameter::Type::COIL;
        default_type_str = "switch";
    } else if (reg_type_str == "discrete") {
        type = TModbusParameter::Type::DISCRETE_INPUT;
        default_type_str = "switch";
    } else if (reg_type_str == "holding") {
        type = TModbusParameter::Type::HOLDING_REGITER;
        default_type_str = "text";
    } else if (reg_type_str == "input") {
        type = TModbusParameter::Type::INPUT_REGISTER;
        default_type_str = "text";
    } else
        throw TConfigParserException("invalid register type: " + reg_type_str);

    bool should_poll = true;
    std::string type_str = channel_data["type"].asString();
    if (type_str.empty())
        type_str = default_type_str;
    if (type_str == "wo-switch") {
        type_str = "switch";
        should_poll = false;
    }

    double scale = 1;
    if (channel_data.isMember("scale"))
        scale = channel_data["scale"].asDouble(); // TBD: check for zero, too

    int on_value = -1;
    if (channel_data.isMember("on_value"))
        on_value = channel_data["on_value"].asInt();

    int max = -1;
    if (channel_data.isMember("max"))
        max = channel_data["max"].asInt();

    TModbusParameter::Format format = TModbusParameter::U16;
    if (channel_data.isMember("format")) {
        std::string format_str = channel_data["format"].asString();
        if (format_str == "s16")
            format = TModbusParameter::S16;
        else if (format_str == "u8")
            format = TModbusParameter::U8;
        else if (format_str == "s8")
            format = TModbusParameter::S8;
    }

    int order = device_config.NextOrderValue();
    TModbusParameter param(device_config.SlaveId, type, address, format, should_poll);
    TModbusChannel channel(name, type_str, scale, device_config.Id, order, on_value, max, param);
    if (HandlerConfig.Debug)
        std::cerr << "channel " << channel.Name << " device id: " << channel.DeviceId << std::endl;
    device_config.AddChannel(channel);
}

void TConfigParser::LoadSetupItem(TDeviceConfig& device_config, const Json::Value& item_data)
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
    TDeviceSetupItem item(name, address, value);
    device_config.AddSetupItem(item);
}

void TConfigParser::LoadDevice(TPortConfig& port_config,
                               const Json::Value& device_data,
                               const std::string& default_id)
{
    if (!device_data.isObject())
        throw TConfigParserException("malformed config");

    if (!device_data.isMember("name"))
        throw TConfigParserException("device name not specified");

    if (device_data.isMember("enabled") && !device_data["enabled"].asBool())
        return;

    TDeviceConfig device_config;
    device_config.Id = device_data.isMember("id") ? device_data["id"].asString() : default_id;
    device_config.Name = device_data["name"].asString();
    device_config.SlaveId = device_data["slave_id"].asInt();

    if (device_data.isMember("setup")) {
        const Json::Value array = device_data["setup"];
        for(unsigned int index = 0; index < array.size(); ++index)
            LoadSetupItem(device_config, array[index]);
    }

    const Json::Value array = device_data["channels"];
    for(unsigned int index = 0; index < array.size(); ++index)
        LoadChannel(device_config, array[index]);

    port_config.AddDeviceConfig(device_config);
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

    TPortConfig port_config;
    port_config.Path = port_data["path"].asString();

    if (port_data.isMember("baud_rate"))
        port_config.BaudRate = port_data["baud_rate"].asInt();

    if (port_data.isMember("parity"))
        port_config.DataBits = port_data["parity"].asCString()[0]; // FIXME (can be '\0')
        
    if (port_data.isMember("data_bits"))
        port_config.DataBits = port_data["data_bits"].asInt();

    if (port_data.isMember("stop_bits"))
        port_config.StopBits = port_data["stop_bits"].asInt();

    if (port_data.isMember("poll_interval"))
        port_config.PollInterval = port_data["poll_interval"].asInt();

    const Json::Value array = port_data["devices"];
    for(unsigned int index = 0; index < array.size(); ++index)
        LoadDevice(port_config, array[index], id_prefix + std::to_string(index));

    HandlerConfig.AddPortConfig(port_config);
}

void TConfigParser::LoadConfig()
{
    if (!root.isObject())
        throw TConfigParserException("malformed config");

    if (!root.isMember("ports"))
        throw TConfigParserException("no ports specified");

    if (root.isMember("debug"))
        HandlerConfig.Debug = HandlerConfig.Debug || root["debug"].asBool();

    const Json::Value array = root["ports"];
    for(unsigned int index = 0; index < array.size(); ++index)
        LoadPort(array[index], "wb-modbus-" + std::to_string(index) + "-");
}
