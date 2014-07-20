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

using namespace std;


struct TModbusRegister
{
    string Name;

    TModbusRegister()
        : Name("")
    {};

};

class THandlerConfig
{
    public:
        vector<TModbusRegister> ModbusRegs;
        void AddRegister(TModbusRegister& modbus_reg) { ModbusRegs.push_back(modbus_reg); };

        string DeviceName;

};

class TMQTTModbusHandler : public TMQTTWrapper
{
	public:
        TMQTTModbusHandler(const TMQTTModbusHandler::TConfig& mqtt_config, const THandlerConfig& handler_config);
		~TMQTTModbusHandler();

		void OnConnect(int rc);
		void OnMessage(const struct mosquitto_message *message);
		void OnSubscribe(int mid, int qos_count, const int *granted_qos);

        void UpdateChannelValues();
        string GetChannelTopic(const TModbusRegister& modbus_reg);

    private:
        THandlerConfig Config;
};







TMQTTModbusHandler::TMQTTModbusHandler(const TMQTTModbusHandler::TConfig& mqtt_config, const THandlerConfig& handler_config)
    : TMQTTWrapper(mqtt_config)
    , Config(handler_config)
{
    // init gpios
    // for (const TModbusRegister& modbus_reg : handler_config.ModbusRegs) {
    //     TSysfsGpio gpio_handler(modbus_reg.Gpio, modbus_reg.Inverted);
    //     gpio_handler.Export();
    //     if (gpio_handler.IsExported()) {
    //         gpio_handler.SetOutput();
    //         Regs.push_back(make_pair(modbus_reg, gpio_handler));
    //     } else {
    //         cerr << "ERROR: unable to export gpio " << modbus_reg.Gpio << endl;
    //     }
    // }


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


        for (TModbusRegister& modbus_reg : Config.ModbusRegs) {
            string control_prefix = prefix + "controls/" + modbus_reg.Name;
            Publish(NULL, control_prefix + "/meta/type", "switch", 0, true);
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
        for (TModbusRegister& modbus_reg : Config.ModbusRegs) {
            if (tokens[4] == modbus_reg.Name) {
                // auto & gpio_handler = channel_desc.second;

                // int val = payload == "0" ? 0 : 1;
                Publish(NULL, GetChannelTopic(modbus_reg), payload, 0, true);
                // if (gpio_handler.SetValue(val) == 0) {
                //     // echo, retained
                //     Publish(NULL, GetChannelTopic(modbus_reg), payload, 0, true);
                // }


            }
        }
    }
}

void TMQTTModbusHandler::OnSubscribe(int mid, int qos_count, const int *granted_qos)
{
	printf("Subscription succeeded.\n");
}

string TMQTTModbusHandler::GetChannelTopic(const TModbusRegister& modbus_reg) {
    static string controls_prefix = string("/devices/") + MQTTConfig.Id + "/controls/";
    return (controls_prefix + modbus_reg.Name);
}

void TMQTTModbusHandler::UpdateChannelValues() {
    static int did_update = 0;
    if (did_update++)
        return;
    for (TModbusRegister& modbus_reg : Config.ModbusRegs) {
        int value = 4242;
        Publish(NULL, GetChannelTopic(modbus_reg), to_string(value), 0, true); // Publish current value (make retained)

        // vv order matters
        // int cached = gpio_handler.GetCachedValue();
        // int value = gpio_handler.GetValue();

        // if (value >= 0) {
        //     if ( (cached < 0) || (cached != value)) {
        //         Publish(NULL, GetChannelTopic(modbus_reg), to_string(value), 0, true); // Publish current value (make retained)
        //     }
        // }
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
    //~ if (optind < argc) {
        //~ printf ("non-option ARGV-elements: ");
        //~ while (optind < argc)
            //~ printf ("%s ", argv[optind++]);
        //~ printf ("\n");
    //~ }






    {
        // Let's parse it
        Json::Value root;
        Json::Reader reader;

        if (config_fname.empty()) {
            cerr << "Please specify config file with -c option" << endl;
            return 1;
        }

        ifstream myfile (config_fname);

        bool parsedSuccess = reader.parse(myfile,
                                       root,
                                       false);

        if(not parsedSuccess)
        {
            // Report failures and their locations
            // in the document.
            cerr << "Failed to parse JSON" << endl
               << reader.getFormatedErrorMessages()
               << endl;
            return 1;
        }


        handler_config.DeviceName = root["device_name"].asString();

         // Let's extract the array contained
         // in the root object
        const Json::Value array = root["channels"];

         // Iterate over sequence elements and
         // print its values
        for(unsigned int index=0; index<array.size();
             ++index)
        {
            TModbusRegister modbus_reg;
            // modbus_reg.Gpio = array[index]["gpio"].asInt();
            modbus_reg.Name = array[index]["name"].asString();
            //~ modbus_reg.Inverted = array[index]["inverted"].asString();

            handler_config.AddRegister(modbus_reg);

        }
    }



	mosqpp::lib_init();

    mqtt_config.Id = "wb-modbus";
	mqtt_handler = new TMQTTModbusHandler(mqtt_config, handler_config);

    cout << "Press Enter to stop..." << endl;
    mqtt_handler->StartLoop();
    cin.ignore();
    cout << "Exiting..." << endl;
    mqtt_handler->StopLoop();

	mosqpp::lib_cleanup();

	return 0;
}
//build-dep libmosquittopp-dev libmosquitto-dev
// dep: libjsoncpp0 libmosquittopp libmosquitto
