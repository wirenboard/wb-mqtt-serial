#include <cmath>
#include <unordered_map>
#include <memory>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>

#include <getopt.h>
#include <unistd.h>

// This is the JSON header
#include "jsoncpp/json/json.h"

#include <mosquittopp.h>

#include "common/utils.h"
#include "common/mqtt_wrapper.h"
#include "modbus_client.h"

using namespace std;

struct TModbusChannel
{
    TModbusChannel(string name = "", string type = "text",
                   double scale = 1, string device_id = "", int order = 0,
                   int on_value = -1, int max = - 1,
                   TModbusParameter param = TModbusParameter())
        : Name(name), Type(type), Scale(scale), DeviceId(device_id),
          Order(order), OnValue(on_value), Max(max), Parameter(param) {}

    string Name;
    string Type;
    double Scale;
    string DeviceId; // FIXME
    int Order;
    int OnValue;
    int Max;
    TModbusParameter Parameter;
};

struct TDeviceSetupItem
{
    TDeviceSetupItem(string name, int address, int value)
        : Name(name), Address(address), Value(value) {}
    string Name;
    int Address;
    int Value;
};

struct TDeviceConfig
{
    TDeviceConfig(string name = "")
        : Name(name) {}
    int NextOrderValue() const { return ModbusChannels.size() + 1; }
    void AddChannel(const TModbusChannel& channel) { ModbusChannels.push_back(channel); };
    void AddSetupItem(const TDeviceSetupItem& item) { SetupItems.push_back(item); }
    string Id;
    string Name;
    int SlaveId;
    vector<TModbusChannel> ModbusChannels;
    vector<TDeviceSetupItem> SetupItems;
};
    
struct TPortConfig
{
    void AddDeviceConfig(const TDeviceConfig& device_config) { DeviceConfigs.push_back(device_config); }
    string Path;
    int BaudRate = 115200;
    char Parity = 'N';
    int DataBits = 8;
    int StopBits = 1;
    int PollInterval = 2000;
    bool Debug = false;
    vector<TDeviceConfig> DeviceConfigs;
};

struct THandlerConfig
{
    void AddPortConfig(const TPortConfig& port_config) {
        PortConfigs.push_back(port_config);
        PortConfigs[PortConfigs.size() - 1].Debug = Debug;
    }
    bool Debug = false;
    vector<TPortConfig> PortConfigs;
};
    
class TModbusPort
{
public:
    TModbusPort(TMQTTWrapper* wrapper, const TPortConfig& port_config, bool debug);
    void Loop();
    void Start();
    void PubSubSetup();
    bool HandleMessage(const string& topic, const string& payload);
    string GetChannelTopic(const TModbusChannel& channel);
    bool WriteInitValues();
private:
    void OnModbusValueChange(const TModbusParameter& param, int int_value);
    TMQTTWrapper* Wrapper;
    TPortConfig Config;
    unique_ptr<TModbusClient> Client;
    unordered_map<TModbusParameter, TModbusChannel> ParameterToChannelMap;
    unordered_map<string, TModbusChannel> NameToChannelMap;
    std::thread Worker;
};

class TMQTTModbusHandler : public TMQTTWrapper
{
public:
    TMQTTModbusHandler(const TMQTTModbusHandler::TConfig& mqtt_config, const THandlerConfig& handler_config);

    void OnConnect(int rc);
    void OnMessage(const struct mosquitto_message *message);
    void OnSubscribe(int mid, int qos_count, const int *granted_qos);

    void Start();
    bool WriteInitValues();
private:
    THandlerConfig Config;
    vector< unique_ptr<TModbusPort> > Ports;
};

class TConfigParserException: public exception {
public:
    TConfigParserException(string _message): message(_message) {}
    const char* what () const throw () {
        return ("Error parsing config file: " + message).c_str();
    }
private:
    string message;
};

class TConfigParser
{
public:
    TConfigParser(string config_fname): ConfigFileName(config_fname) {}
    const THandlerConfig& parse();
    void LoadChannel(TDeviceConfig& device_config, const Json::Value& channel_data);
    void LoadSetupItem(TDeviceConfig& device_config, const Json::Value& item_data);
    void LoadDevice(TPortConfig& port_config, const Json::Value& device_data, const string& default_id);
    void LoadPort(const Json::Value& port_data, const string& id_prefix);
    void LoadConfig();
private:
    THandlerConfig HandlerConfig;
    string ConfigFileName;
    Json::Value root;
};

TModbusPort::TModbusPort(TMQTTWrapper* wrapper, const TPortConfig& port_config, bool debug)
    : Wrapper(wrapper),
      Config(port_config)
    , Client(new TModbusClient(Config.Path, Config.BaudRate, Config.Parity, Config.DataBits, Config.StopBits))
{
    Client->SetCallback([this](const TModbusParameter& param, int value) {
            OnModbusValueChange(param, value);
        });
    for (const auto& device_config: Config.DeviceConfigs) {
        for (const auto& channel: device_config.ModbusChannels) {
            ParameterToChannelMap[channel.Parameter] = channel;
            NameToChannelMap[device_config.Id + "/" + channel.Name] = channel;
            Client->AddParam(channel.Parameter);
        }
    }
    Client->SetPollInterval(Config.PollInterval);
    Client->SetModbusDebug(debug);
};

void TModbusPort::PubSubSetup()
{
    for (const auto& device_config : Config.DeviceConfigs) {
        /* Only attempt to Subscribe on a successful connect. */
        string id = device_config.Id.empty() ? Wrapper->Id() : device_config.Id;
        string prefix = string("/devices/") + id + "/";
        // Meta
        Wrapper->Publish(NULL, prefix + "meta/name", device_config.Name, 0, true);
        for (const auto& channel : device_config.ModbusChannels) {
            string control_prefix = prefix + "controls/" + channel.Name;
            switch (channel.Parameter.type) {
            case TModbusParameter::Type::DISCRETE_INPUT:
                Wrapper->Publish(NULL, control_prefix + "/meta/readonly", "1", 0, true);
            case TModbusParameter::Type::COIL:
                Wrapper->Publish(NULL, control_prefix + "/meta/type",
                        channel.Type.empty() ? "switch" : channel.Type, 0, true);
                break;
            case TModbusParameter::Type::INPUT_REGISTER:
                Wrapper->Publish(NULL, control_prefix + "/meta/readonly", "1", 0, true);
            case TModbusParameter::Type::HOLDING_REGITER:
                Wrapper->Publish(NULL, control_prefix + "/meta/type",
                        channel.Type.empty() ? "text" : channel.Type, 0, true);
                Wrapper->Publish(NULL, control_prefix + "/meta/max",
                                 channel.Max < 0 ? "65535" : to_string(channel.Max),
                                 0, true);
                break;
            }
            Wrapper->Publish(NULL, control_prefix + "/meta/order",
                             to_string(channel.Order), 0, true);
            Wrapper->Subscribe(NULL, control_prefix + "/on");
        }
    }

//~ /devices/293723-demo/controls/Demo-Switch 0
//~ /devices/293723-demo/controls/Demo-Switch/on 1
//~ /devices/293723-demo/controls/Demo-Switch/meta/type switch
}

bool TModbusPort::HandleMessage(const string& topic, const string& payload)
{
    const vector<string>& tokens = StringSplit(topic, '/');
    if ((tokens.size() != 6) ||
        (tokens[0] != "") || (tokens[1] != "devices") ||
        (tokens[3] != "controls") || (tokens[5] != "on"))
        return false;

    string device_id = tokens[2];
    string param_name = tokens[4];
    const auto& dev_config_it =
        find_if(Config.DeviceConfigs.begin(),
                Config.DeviceConfigs.end(),
                [device_id](const TDeviceConfig &c) {
                    return c.Id == device_id;
                });

    if (dev_config_it == Config.DeviceConfigs.end())
        return false;

    const auto& it = NameToChannelMap.find(dev_config_it->Id + "/" + param_name);
    if (it == NameToChannelMap.end())
        return false;

    // FIXME: untested
    const TModbusParameter& param = it->second.Parameter;
    int int_val;
    try {
        if (it->second.Scale == 1)
            int_val = stoi(payload);
        else {
            double val = stod(payload);
            int_val = round(val / it->second.Scale);
        }
    } catch (exception&) {
        cerr << "warning: invalid payload for topic " << topic << ": " << payload << endl;
        // here 'true' means that the message doesn't need to be passed
        // to other port handlers
        return true;
    }
    int_val = min(65535, max(0, int_val));
    if (Config.Debug)
        cerr << "setting modbus register: " << param.str() << " <- " << int_val << endl;
    Client->SetValue(param, int_val);
    Wrapper->Publish(NULL, GetChannelTopic(it->second), payload, 0, true);
    return true;
}

string TModbusPort::GetChannelTopic(const TModbusChannel& channel)
{
    string controls_prefix = string("/devices/") + channel.DeviceId + "/controls/";
    return (controls_prefix + channel.Name);
}

void TModbusPort::OnModbusValueChange(const TModbusParameter& param, int int_value)
{
    if (Config.Debug)
        cerr << "modbus value change: " << param.str() << " <- " << int_value << endl;
    const auto& it = ParameterToChannelMap.find(param);
    if (it == ParameterToChannelMap.end()) {
        cerr << "warning: unexpected parameter from modbus" << endl;
        return;
    }
    string payload;
    if (it->second.OnValue >= 0) {
        payload = int_value == it->second.OnValue ? "1" : "0";
        if (Config.Debug)
            cerr << "OnValue: " << it->second.OnValue << "; payload: " << payload << endl;
    } else if (it->second.Scale == 1)
        payload = to_string(int_value);
    else {
        double value = int_value * it->second.Scale;
        if (Config.Debug)
            cerr << "after scaling: " << value << endl;
        payload = to_string(value);
    }
    // Publish current value (make retained)
    if (Config.Debug)
        cerr << "channel " << it->second.Name << " device id: " <<
            it->second.DeviceId << " -- topic: " << GetChannelTopic(it->second) <<
            " <-- " << payload << endl;
    Wrapper->Publish(NULL, GetChannelTopic(it->second), payload, 0, true);
}

void TModbusPort::Loop()
{
    try {
        Client->Loop();
    } catch (TModbusException& e) {
        cerr << "FATAL: " << e.what() << ". Stopping event loops." << endl;
        exit(1);
    }
}

void TModbusPort::Start()
{
    Worker = std::thread([this]() { Loop(); });
}

bool TModbusPort::WriteInitValues()
{
    bool did_write = false;
    for (const auto& device_config : Config.DeviceConfigs) {
        for (const auto& setup_item : device_config.SetupItems) {
            if (Config.Debug)
                cerr << "Init: " << setup_item.Name << ": holding register " <<
                    setup_item.Address << " <-- " << setup_item.Value << endl;
            Client->WriteHoldingRegister(device_config.SlaveId,
                                         setup_item.Address,
                                         setup_item.Value);
            did_write = true;
        }
    }

    return did_write;
}

TMQTTModbusHandler::TMQTTModbusHandler(const TMQTTModbusHandler::TConfig& mqtt_config, const THandlerConfig& handler_config)
    : TMQTTWrapper(mqtt_config),
      Config(handler_config)
{
    for (const auto& port_config : Config.PortConfigs)
        Ports.push_back(unique_ptr<TModbusPort>(new TModbusPort(this, port_config, Config.Debug)));

	Connect();
}

void TMQTTModbusHandler::OnConnect(int rc)
{
    if (Config.Debug)
        cerr << "Connected with code " << rc << endl;

	if(rc != 0)
        return;

    for (const auto& port: Ports)
        port->PubSubSetup();
}

void TMQTTModbusHandler::OnMessage(const struct mosquitto_message *message)
{
    string topic = message->topic;
    string payload = static_cast<const char *>(message->payload);
    for (const auto& port: Ports) {
        if (port->HandleMessage(topic, payload))
            break;
    }
}

void TMQTTModbusHandler::OnSubscribe(int, int, const int *)
{
	if (Config.Debug)
        cerr << "Subscription succeeded." << endl;
}

void TMQTTModbusHandler::Start()
{
    for (const auto& port: Ports)
        port->Start();
}

bool TMQTTModbusHandler::WriteInitValues()
{
    bool did_write = false;
    vector< pair<int, int> > values;
    for (const auto& port: Ports) {
        if (port->WriteInitValues())
            did_write = true;
    }

    return did_write;
}

const THandlerConfig& TConfigParser::parse()
{
    // Let's parse it
    Json::Reader reader;

    if (ConfigFileName.empty())
        throw TConfigParserException("Please specify config file with -c option");

    ifstream myfile (ConfigFileName);
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

    string name = channel_data["name"].asString();
    int address = channel_data["address"].asInt();
    string reg_type_str = channel_data["reg_type"].asString();
    string default_type_str = "text";
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
    string type_str = channel_data["type"].asString();
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
        string format_str = channel_data["format"].asString();
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
        cerr << "channel " << channel.Name << " device id: " << channel.DeviceId << endl;
    device_config.AddChannel(channel);
}

void TConfigParser::LoadSetupItem(TDeviceConfig& device_config, const Json::Value& item_data)
{
    if (!item_data.isObject())
        throw TConfigParserException("malformed config");

    string name = item_data.isMember("name") ?
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
                               const string& default_id)
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
                             const string& id_prefix)
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
        LoadDevice(port_config, array[index], id_prefix + to_string(index));

    HandlerConfig.AddPortConfig(port_config);
}

void TConfigParser::LoadConfig()
{
    if (!root.isObject())
        throw TConfigParserException("malformed config");

    if (!root.isMember("ports"))
        throw TConfigParserException("no ports specified");

    // Note that debug mode may be set to true via command
    // line flag. That's done before parsing the config.
    if (root.isMember("debug"))
        HandlerConfig.Debug = HandlerConfig.Debug || root["debug"].asBool();

    const Json::Value array = root["ports"];
    for(unsigned int index = 0; index < array.size(); ++index)
        LoadPort(array[index], "wb-modbus-" + to_string(index) + "-");
}

int main(int argc, char *argv[])
{
	class TMQTTModbusHandler* mqtt_handler;
    THandlerConfig handler_config;
    TMQTTModbusHandler::TConfig mqtt_config;
    mqtt_config.Host = "localhost";
    mqtt_config.Port = 1883;
    string config_fname;
    bool debug = false;
    bool init = false;

    int c;
    //~ int digit_optind = 0;
    //~ int aopt = 0, bopt = 0;
    //~ char *copt = 0, *dopt = 0;
    while ( (c = getopt(argc, argv, "dsc:h:p:")) != -1) {
        //~ int this_option_optind = optind ? optind : 1;
        switch (c) {
        case 'd':
            debug = true;
            break;
        case 's':
            init = true;
            break;
        case 'c':
            config_fname = optarg;
            break;
        case 'p':
            mqtt_config.Port = stoi(optarg);
            break;
        case 'h':
            mqtt_config.Host = optarg;
            break;
        case '?':
            break;
        default:
            printf ("?? getopt returned character code 0%o ??\n", c);
        }
    }

    try {
        TConfigParser parser(config_fname);
        handler_config = parser.parse();
    } catch (const TConfigParserException& e) {
        cerr << "FATAL: " << e.what() << endl;
        return 1;
    }

    handler_config.Debug = handler_config.Debug || debug;

	mosqpp::lib_init();

    if (mqtt_config.Id.empty())
        mqtt_config.Id = "wb-modbus";

    try {
        mqtt_handler = new TMQTTModbusHandler(mqtt_config, handler_config);
        if (init) {
            if (!mqtt_handler->WriteInitValues())
                cerr << "NOTE: no init sections were found for enabled devices" << endl;
        } else {
            mqtt_handler->StartLoop();
            mqtt_handler->Start();
        }
    } catch (const TModbusException& e) {
        cerr << "FATAL: " << e.what() << endl;
        return 1;
    }

    if (!init)
        for (;;) sleep(1);

#if 0
    // FIXME: handle Ctrl-C etc.
    mqtt_handler->StopLoop();
	mosqpp::lib_cleanup();
#endif

	return 0;
}
//build-dep libmosquittopp-dev libmosquitto-dev
// dep: libjsoncpp0 libmosquittopp libmosquitto

// TBD: fix race condition that occurs after modbus error on startup
// (slave not active)
// TBD: check json structure
// TBD: proper error checking everywhere (catch exceptions, etc.)
// TBD: automatic tests(?)
