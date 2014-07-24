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
                   double scale = 1, TModbusParameter param = TModbusParameter())
        : Name(name), Type(type), Scale(scale), Parameter(param) {}

    string Name;
    string Type;
    double Scale;
    TModbusParameter Parameter;
};

struct THandlerConfig
{
    void AddChannel(const TModbusChannel& channel) { ModbusChannels.push_back(channel); };
    string DeviceName;
    string SerialPort;
    int BaudRate = 115200;
    char Parity = 'N';
    int DataBits = 8;
    int StopBits = 1;
    int PollInterval = 2000;
    bool Debug = false;
    vector<TModbusChannel> ModbusChannels;
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


TMQTTModbusHandler::TMQTTModbusHandler(const TMQTTModbusHandler::TConfig& mqtt_config, const THandlerConfig& handler_config)
    : TMQTTWrapper(mqtt_config)
    , Config(handler_config)
    , Client(new TModbusClient(Config.SerialPort, Config.BaudRate, Config.Parity, Config.DataBits, Config.StopBits))
{
    Client->SetCallback([this](const TModbusParameter& param, int value) {
            OnModbusValueChange(param, value);
        });
    for (const auto& channel: Config.ModbusChannels) {
        ParameterToChannelMap[channel.Parameter] = channel;
        NameToChannelMap[channel.Name] = channel;
        Client->AddParam(channel.Parameter);
    }
    Client->SetPollInterval(handler_config.PollInterval);
    Client->SetModbusDebug(handler_config.Debug);

	Connect();
};

void TMQTTModbusHandler::OnConnect(int rc)
{
	printf("Connected with code %d.\n", rc);
	if(rc == 0){
		/* Only attempt to Subscribe on a successful connect. */
        string prefix = string("/devices/") + MQTTConfig.Id + "/";
        
        // Meta
        Publish(NULL, prefix + "/meta/name", Config.DeviceName, 0, true);


        for (TModbusChannel& channel : Config.ModbusChannels) {
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

//~ /devices/293723-demo/controls/Demo-Switch 0
//~ /devices/293723-demo/controls/Demo-Switch/on 1
//~ /devices/293723-demo/controls/Demo-Switch/meta/type switch



	}
}

void TMQTTModbusHandler::OnMessage(const struct mosquitto_message *message)
{
    string topic = message->topic;
    string payload = static_cast<const char *>(message->payload);


    const vector<string>& tokens = StringSplit(topic, '/');

    if (  (tokens.size() == 6) &&
          (tokens[0] == "") && (tokens[1] == "devices") &&
          (tokens[2] == MQTTConfig.Id) && (tokens[3] == "controls") &&
          (tokens[5] == "on") )
    {
        const auto& it = NameToChannelMap.find(tokens[4]);
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
    static string controls_prefix = string("/devices/") + MQTTConfig.Id + "/controls/";
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

bool ParseConfig(THandlerConfig& handler_config, string config_fname)
{
    // Let's parse it
    Json::Value root;
    Json::Reader reader;

    if (config_fname.empty()) {
        cerr << "Please specify config file with -c option" << endl;
        return false;
    }

    ifstream myfile (config_fname);

    bool parsedSuccess = reader.parse(myfile, root, false);

    if(not parsedSuccess) {
        // Report failures and their locations
        // in the document.
        cerr << "Failed to parse JSON" << endl
             << reader.getFormatedErrorMessages()
             << endl;
        return false;
    }


    if (!root.isObject()) {
        cerr << "malformed config" << endl;
        return false;
    }
            
    if (!root.isMember("device_name")) {
        cerr << "device_name not specified" << endl;
        return false;
    }
    handler_config.DeviceName = root["device_name"].asString();
    if (!root.isMember("serial_port")) {
        cerr << "serial_port not specified" << endl;
        return false;
    }
    handler_config.SerialPort = root["serial_port"].asString();

    if (root.isMember("baud_rate"))
        handler_config.BaudRate = root["baud_rate"].asInt();

    if (root.isMember("parity"))
        handler_config.DataBits = root["parity"].asCString()[0]; // FIXME (can be '\0')
        
    if (root.isMember("data_bits"))
        handler_config.DataBits = root["data_bits"].asInt();

    if (root.isMember("stop_bits"))
        handler_config.StopBits = root["stop_bits"].asInt();

    if (root.isMember("poll_interval"))
        handler_config.PollInterval = root["poll_interval"].asInt();

    if (root.isMember("debug"))
        handler_config.Debug = handler_config.Debug || root["debug"].asBool();

    // Let's extract the array contained
    // in the root object
    const Json::Value array = root["channels"];

    // Iterate over sequence elements and
    // print its values
    for(unsigned int index=0; index<array.size(); ++index) {
        string name = array[index]["name"].asString();
        int address = array[index]["address"].asInt();
        int slave = array[index]["slave"].asInt();
        string reg_type_str = array[index]["reg_type"].asString();
        TModbusParameter::Type type;
        if (reg_type_str == "coil")
            type = TModbusParameter::Type::COIL;
        else if (reg_type_str == "discrete")
            type = TModbusParameter::Type::DISCRETE_INPUT;
        else if (reg_type_str == "holding")
            type = TModbusParameter::Type::HOLDING_REGITER;
        else if (reg_type_str == "input")
            type = TModbusParameter::Type::INPUT_REGISTER;
        else {
            cerr << "invalid register type: " << reg_type_str << endl;
            return false;
        }
        string type_str = array[index]["type"].asString();
        if (type_str.empty())
            type_str = "text";
        double scale = 1;
        if (array[index].isMember("scale"))
            scale = array[index]["scale"].asDouble(); // TBD: check for zero, too
        TModbusChannel channel(name, type_str, scale, TModbusParameter(slave, type, address));
        handler_config.AddChannel(channel);
    }

    return true;
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

    if (!ParseConfig(handler_config, config_fname))
        return 1;

	mosqpp::lib_init();

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
