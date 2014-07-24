#include <cmath>
#include <unordered_map>
#include <memory>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <algorithm>

#include <getopt.h>

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
                   double scale = 1, string device_id = "",
                   TModbusParameter param = TModbusParameter())
        : Name(name), Type(type), Scale(scale), DeviceId(device_id), Parameter(param) {}

    string Name;
    string Type;
    double Scale;
    string DeviceId; // FIXME
    TModbusParameter Parameter;
};

struct TDeviceConfig
{
    TDeviceConfig(string name = "")
        : Name(name) {}
    void AddChannel(const TModbusChannel& channel) { ModbusChannels.push_back(channel); };
    string Id;
    string Name;
    int SlaveId;
    vector<TModbusChannel> ModbusChannels;
};
    
struct THandlerConfig
{
    void AddDeviceConfig(const TDeviceConfig& config) { DeviceConfigs.push_back(config); }
    string SerialPort;
    int BaudRate = 115200;
    char Parity = 'N';
    int DataBits = 8;
    int StopBits = 1;
    int PollInterval = 2000;
    bool Debug = false;
    vector<TDeviceConfig> DeviceConfigs;
};

class TMQTTModbusHandler : public TMQTTWrapper
{
public:
    TMQTTModbusHandler(const TMQTTModbusHandler::TConfig& mqtt_config, const THandlerConfig& handler_config);
    ~TMQTTModbusHandler();

    void OnConnect(int rc);
    void OnMessage(const struct mosquitto_message *message);
    void OnSubscribe(int mid, int qos_count, const int *granted_qos);

    string GetChannelTopic(const TModbusChannel& channel);
    void Loop();
private:
    void OnModbusValueChange(const TModbusParameter& param, int int_value);
    THandlerConfig Config;
    unique_ptr<TModbusClient> Client;
    unordered_map<TModbusParameter, TModbusChannel> ParameterToChannelMap;
    unordered_map<string, TModbusChannel> NameToChannelMap;
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
    void LoadDevice(const Json::Value& device_data, string default_id);
    void LoadConfig();
private:
    THandlerConfig HandlerConfig;
    string ConfigFileName;
    Json::Value root;
};

TMQTTModbusHandler::TMQTTModbusHandler(const TMQTTModbusHandler::TConfig& mqtt_config, const THandlerConfig& handler_config)
    : TMQTTWrapper(mqtt_config)
    , Config(handler_config)
    , Client(new TModbusClient(Config.SerialPort, Config.BaudRate, Config.Parity, Config.DataBits, Config.StopBits))
{
    Client->SetCallback([this](const TModbusParameter& param, int value) {
            OnModbusValueChange(param, value);
        });
    for (const auto& device_config: Config.DeviceConfigs) {
        for (const auto& channel: device_config.ModbusChannels) {
            ParameterToChannelMap[channel.Parameter] = channel;
            NameToChannelMap[channel.Name] = channel;
            Client->AddParam(channel.Parameter);
        }
    }
    Client->SetPollInterval(handler_config.PollInterval);
    Client->SetModbusDebug(handler_config.Debug);

	Connect();
};

void TMQTTModbusHandler::OnConnect(int rc)
{
	printf("Connected with code %d.\n", rc);

	if(rc != 0)
        return;

    for (const auto& device_config : Config.DeviceConfigs) {
        /* Only attempt to Subscribe on a successful connect. */
        string id = device_config.Id.empty() ? MQTTConfig.Id : device_config.Id;
        string prefix = string("/devices/") + id + "/";
        // Meta
        Publish(NULL, prefix + "meta/name", device_config.Name, 0, true);
        for (const auto& channel : device_config.ModbusChannels) {
            string control_prefix = prefix + "controls/" + channel.Name;
            switch (channel.Parameter.type) {
            case TModbusParameter::Type::COIL:
            case TModbusParameter::Type::DISCRETE_INPUT:
                Publish(NULL, control_prefix + "/meta/type",
                        channel.Type.empty() ? "switch" : channel.Type, 0, true);
                break;
            case TModbusParameter::Type::HOLDING_REGITER:
            case TModbusParameter::Type::INPUT_REGISTER:
                Publish(NULL, control_prefix + "/meta/type",
                        channel.Type.empty() ? "text" : channel.Type, 0, true);
                Publish(NULL, control_prefix + "/meta/max", "65535", 0, true);
                break;
            }
            Subscribe(NULL, control_prefix + "/on");
        }
    }

//~ /devices/293723-demo/controls/Demo-Switch 0
//~ /devices/293723-demo/controls/Demo-Switch/on 1
//~ /devices/293723-demo/controls/Demo-Switch/meta/type switch
}

void TMQTTModbusHandler::OnMessage(const struct mosquitto_message *message)
{
    string topic = message->topic;
    string payload = static_cast<const char *>(message->payload);

    const vector<string>& tokens = StringSplit(topic, '/');
    if ((tokens.size() != 6) ||
        (tokens[0] != "") || (tokens[1] != "devices") ||
        (tokens[3] != "controls") || (tokens[5] != "on"))
        return;

    string device_id = tokens[2];
    string param_name = tokens[4];
    for(const auto& device_config : Config.DeviceConfigs) {
        if (device_config.Id != device_id)
            continue;

        const auto& it = NameToChannelMap.find(param_name);
        if (it == NameToChannelMap.end())
            return;

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
            return;
        }
        int_val = min(65535, max(0, int_val));
        cout << "setting modbus register: " << param.str() << " <- " << int_val << endl;
        Client->SetValue(param, int_val);
        Publish(NULL, GetChannelTopic(it->second), payload, 0, true);
    }
}

void TMQTTModbusHandler::OnSubscribe(int, int, const int *)
{
	printf("Subscription succeeded.\n");
}

string TMQTTModbusHandler::GetChannelTopic(const TModbusChannel& channel)
{
    string controls_prefix = string("/devices/") + channel.DeviceId + "/controls/";
    return (controls_prefix + channel.Name);
}

void TMQTTModbusHandler::OnModbusValueChange(const TModbusParameter& param, int int_value)
{
    cout << "modbus value change: " << param.str() << " <- " << int_value << endl;
    const auto& it = ParameterToChannelMap.find(param);
    if (it == ParameterToChannelMap.end()) {
        cerr << "warning: unexpected parameter from modbus" << endl;
        return;
    }
    string payload;
    if (it->second.Scale == 1)
        payload = to_string(int_value);
    else {
        double value = int_value * it->second.Scale;
        cout << "after scaling: " << value << endl;
        payload = to_string(value);
    }
    // Publish current value (make retained)
    cout << "channel " << it->second.Name << " device id: " << it->second.DeviceId << " -- topic: " << GetChannelTopic(it->second) << endl;
    Publish(NULL, GetChannelTopic(it->second), payload, 0, true);
}

void TMQTTModbusHandler::Loop()
{
    try {
        Client->Loop();
    } catch (TModbusException& e) {
        cerr << "Fatal: " << e.what() << endl;
    }
}

const THandlerConfig& TConfigParser::parse()
{
    // Let's parse it
    Json::Reader reader;

    if (ConfigFileName.empty())
        throw TConfigParserException("Please specify config file with -c option");

    ifstream myfile (ConfigFileName);

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

    string name = channel_data["name"].asString();
    int address = channel_data["address"].asInt();
    string reg_type_str = channel_data["reg_type"].asString();
    TModbusParameter::Type type;
    if (reg_type_str == "coil")
        type = TModbusParameter::Type::COIL;
    else if (reg_type_str == "discrete")
        type = TModbusParameter::Type::DISCRETE_INPUT;
    else if (reg_type_str == "holding")
        type = TModbusParameter::Type::HOLDING_REGITER;
    else if (reg_type_str == "input")
        type = TModbusParameter::Type::INPUT_REGISTER;
    else
        throw TConfigParserException("invalid register type: " + reg_type_str);

    string type_str = channel_data["type"].asString();
    if (type_str.empty())
        type_str = "text";
    double scale = 1;
    if (channel_data.isMember("scale"))
        scale = channel_data["scale"].asDouble(); // TBD: check for zero, too

    TModbusParameter param(device_config.SlaveId, type, address);
    TModbusChannel channel(name, type_str, scale, device_config.Id, param);
    cout << "channel " << channel.Name << " device id: " << channel.DeviceId << endl;
    device_config.AddChannel(channel);
}

void TConfigParser::LoadDevice(const Json::Value& device_data, string default_id)
{
    if (!device_data.isObject())
        throw TConfigParserException("malformed config");

    if (!device_data.isMember("name"))
        throw TConfigParserException("device name not specified");

    TDeviceConfig device_config;
    device_config.Id = device_data.isMember("id") ? device_data["id"].asString() : default_id;
    device_config.Name = device_data["name"].asString();
    device_config.SlaveId = device_data["slave_id"].asInt();

    const Json::Value array = device_data["channels"];
    for(unsigned int index = 0; index < array.size(); ++index)
        LoadChannel(device_config, array[index]);

    HandlerConfig.AddDeviceConfig(device_config);
}

void TConfigParser::LoadConfig()
{
    if (!root.isObject())
        throw TConfigParserException("malformed config");

    if (!root.isMember("serial_port"))
        throw TConfigParserException("serial_port not specified");

    HandlerConfig.SerialPort = root["serial_port"].asString();

    if (root.isMember("baud_rate"))
        HandlerConfig.BaudRate = root["baud_rate"].asInt();

    if (root.isMember("parity"))
        HandlerConfig.DataBits = root["parity"].asCString()[0]; // FIXME (can be '\0')
        
    if (root.isMember("data_bits"))
        HandlerConfig.DataBits = root["data_bits"].asInt();

    if (root.isMember("stop_bits"))
        HandlerConfig.StopBits = root["stop_bits"].asInt();

    if (root.isMember("poll_interval"))
        HandlerConfig.PollInterval = root["poll_interval"].asInt();

    if (root.isMember("debug"))
        HandlerConfig.Debug = HandlerConfig.Debug || root["debug"].asBool();

    const Json::Value array = root["devices"];
    for(unsigned int index = 0; index < array.size(); ++index)
        LoadDevice(array[index], "wb-modbus-" + to_string(index));
}

int main(int argc, char *argv[])
{
	class TMQTTModbusHandler* mqtt_handler;
    THandlerConfig handler_config;
    TMQTTModbusHandler::TConfig mqtt_config;
    mqtt_config.Host = "localhost";
    mqtt_config.Port = 1883;
    string config_fname;

    int c;
    //~ int digit_optind = 0;
    //~ int aopt = 0, bopt = 0;
    //~ char *copt = 0, *dopt = 0;
    while ( (c = getopt(argc, argv, "dc:h:p:")) != -1) {
        //~ int this_option_optind = optind ? optind : 1;
        switch (c) {
        case 'd':
            handler_config.Debug = true;
            break;
        case 'c':
            printf ("option c with value '%s'\n", optarg);
            config_fname = optarg;
            break;
        case 'p':
            printf ("option p with value '%s'\n", optarg);
            mqtt_config.Port = stoi(optarg);
            break;
        case 'h':
            printf ("option h with value '%s'\n", optarg);
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
        cerr << e.what() << endl;
        return 1;
    }

	mosqpp::lib_init();

    if (mqtt_config.Id.empty())
        mqtt_config.Id = "wb-modbus";
	mqtt_handler = new TMQTTModbusHandler(mqtt_config, handler_config);

    mqtt_handler->StartLoop();
    mqtt_handler->Loop();
    mqtt_handler->StopLoop();

	mosqpp::lib_cleanup();

	return 0;
}
//build-dep libmosquittopp-dev libmosquitto-dev
// dep: libjsoncpp0 libmosquittopp libmosquitto

// TBD: fix race condition that occurs after modbus error on startup
// (slave not active)
// TBD: check json structure
// TBD: proper error checking everywhere (catch exceptions, etc.)
// TBD: automatic tests(?)
