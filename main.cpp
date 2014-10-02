#include <iostream>
#include <cstdio>

#include <getopt.h>
#include <unistd.h>

#include <mosquittopp.h>

#include "modbus_observer.h"

using namespace std;

int main(int argc, char *argv[])
{
    TMQTTClient::TConfig mqtt_config;
    mqtt_config.Host = "localhost";
    mqtt_config.Port = 1883;
    string config_fname;
    bool debug = false;

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

    PHandlerConfig handler_config;
    try {
        TConfigParser parser(config_fname, debug);
        handler_config = parser.Parse();
    } catch (const TConfigParserException& e) {
        cerr << "FATAL: " << e.what() << endl;
        return 1;
    }

	mosqpp::lib_init();

    if (mqtt_config.Id.empty())
        mqtt_config.Id = "wb-modbus";

    PMQTTClient mqtt_client(new TMQTTClient(mqtt_config));

    try {
        PMQTTModbusObserver modbus_observer(new TMQTTModbusObserver(mqtt_client, handler_config));
        modbus_observer->SetUp();
        if (modbus_observer->WriteInitValues() && handler_config->Debug)
            cerr << "Register-based setup performed." << endl;
        mqtt_client->StartLoop();
        modbus_observer->ModbusLoop();
    } catch (const TModbusException& e) {
        cerr << "FATAL: " << e.what() << endl;
        return 1;
    }

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
