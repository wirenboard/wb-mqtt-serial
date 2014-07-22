#include <unordered_map>
#include <memory>
#include <iostream>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

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
    TModbusChannel(string name, TModbusParameter param)
        : Name(name), Parameter(param) {}

    string Name;
    string Type;
    TModbusParameter Parameter;
};

struct THandlerConfig
{
    void AddRegister(const TModbusChannel& channel) { ModbusChannels.push_back(channel); };
    string DeviceName;
    string SerialPort;
    int BaudRate = 115200;
    char Parity = 'N';
    int DataBits = 8;
    int StopBits = 1;
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

    string GetChannelTopic(string name);
    void Loop();

private:
    void OnModbusValueChange(const TModbusParameter& param, int value);
    THandlerConfig Config;
    unique_ptr<TModbusClient> Client;
    unordered_map<TModbusParameter, string> ParameterToNameMap;
    unordered_map<string, TModbusParameter> NameToParameterMap;
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
        ParameterToNameMap[channel.Parameter] = channel.Name;
        NameToParameterMap[channel.Name] = channel.Parameter;
        Client->AddParam(channel.Parameter);
    }

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
        const auto& it = NameToParameterMap.find(tokens[4]);
        if (it == NameToParameterMap.end())
            return;

        const TModbusParameter& param = it->second;
        int val;
        try {
            val = stoi(payload);
        } catch (exception&) {
            cerr << "warning: invalid payload for topic " << topic << ": " << payload << endl;
            return;
        }
        cout << "setting modbus register: " << param.str() << " <- " << val << endl;
        Client->SetValue(param, val);
        Publish(NULL, GetChannelTopic(it->first), payload, 0, true);
    }
}

void TMQTTModbusHandler::OnSubscribe(int, int, const int *)
{
	printf("Subscription succeeded.\n");
}

string TMQTTModbusHandler::GetChannelTopic(string name)
{
    static string controls_prefix = string("/devices/") + MQTTConfig.Id + "/controls/";
    return (controls_prefix + name);
}

void TMQTTModbusHandler::OnModbusValueChange(const TModbusParameter& param, int value)
{
    cout << "modbus value change: " << param.str() << " <- " << value << endl;
    const auto& it = ParameterToNameMap.find(param);
    if (it == ParameterToNameMap.end()) {
        cerr << "warning: unexpected parameter from modbus" << endl;
        return;
    }
    // Publish current value (make retained)
    Publish(NULL, GetChannelTopic(it->second), to_string(value), 0, true);
}

void TMQTTModbusHandler::Loop()
{
    try {
        Client->Loop();
    } catch (TModbusException& e) {
        cerr << "Fatal: " << e.what() << endl;
    }
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
    while ( (c = getopt(argc, argv, "c:h:p:")) != -1) {
        //~ int this_option_optind = optind ? optind : 1;
        switch (c) {
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

    {
        // Let's parse it
        Json::Value root;
        Json::Reader reader;

        if (config_fname.empty()) {
            cerr << "Please specify config file with -c option" << endl;
            return 1;
        }

        ifstream myfile (config_fname);

        bool parsedSuccess = reader.parse(myfile, root, false);

        if(not parsedSuccess)
        {
            // Report failures and their locations
            // in the document.
            cerr << "Failed to parse JSON" << endl
               << reader.getFormatedErrorMessages()
               << endl;
            return 1;
        }


        if (!root.isObject()) {
            cerr << "malformed config" << endl;
            return 1;
        }
            
        if (!root.isMember("device_name")) {
            cerr << "device_name not specified" << endl;
            return 1;
        }
        handler_config.DeviceName = root["device_name"].asString();
        if (!root.isMember("serial_port")) {
            cerr << "serial_port not specified" << endl;
            return 1;
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

        // Let's extract the array contained
        // in the root object
        const Json::Value array = root["channels"];

         // Iterate over sequence elements and
         // print its values
        for(unsigned int index=0; index<array.size();
             ++index)
        {
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
                return 1;
            }
            string type_str = array[index]["type"].asString();
            if (type_str.empty())
                type_str = "text";
            handler_config.AddRegister(TModbusChannel(name, TModbusParameter(slave, type, address)));
        }
    }

	mosqpp::lib_init();

    mqtt_config.Id = "wb-modbus";
	mqtt_handler = new TMQTTModbusHandler(mqtt_config, handler_config);

    cout << "Press Enter to stop..." << endl;
    mqtt_handler->StartLoop();
    mqtt_handler->Loop();
    cout << "Exiting..." << endl;
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
