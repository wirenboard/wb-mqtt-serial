#include "serial_device.h"
#include "serial_driver.h"
#include "log.h"

#include <wblib/wbmqtt.h>
#include <wblib/signal_handling.h>

#include <getopt.h>
#include <unistd.h>

using namespace std;

const auto driverName      = "wb-modbus";
const auto libwbmqttDbFile = "/var/lib/wb-mqtt-serial/libwbmqtt.db";
const auto templatesFolder = "/usr/share/wb-mqtt-serial/templates";

int main(int argc, char *argv[])
{
    WBMQTT::TMosquittoMqttConfig mqttConfig;
    string configFilename;
    int debug = 0;

    WBMQTT::SignalHandling::Handle({ SIGINT });
    WBMQTT::SignalHandling::OnSignal(SIGINT, [&]{ WBMQTT::SignalHandling::Stop(); });
    WBMQTT::SetThreadName("main");

    int c;
    //~ int digit_optind = 0;
    //~ int aopt = 0, bopt = 0;
    //~ char *copt = 0, *dopt = 0;
    while ( (c = getopt(argc, argv, "d:sc:h:H:p:u:P:T:")) != -1) {
        //~ int this_option_optind = optind ? optind : 1;
        switch (c) {
        case 'd':
            debug = stoi(optarg);
            break;
        case 'c':
            configFilename = optarg;
            break;
        case 'p':
            mqttConfig.Port = stoi(optarg);
            break;
        case 'h':
            mqttConfig.Host = optarg;
            break;

        case 'T':
            mqttConfig.Prefix = optarg;
            break;

        case 'u':
            mqttConfig.User = optarg;
            break;

        case 'P':
            mqttConfig.Password = optarg;
            break;

        case '?':
        default:
            printf ("?? getopt returned character code 0%o ??\n", c);

            printf("Usage:\n wb-mqtt-serial [options]\n");
            printf("Options:\n");
            printf("\t-d level    \t\t\t enable debuging output (1 - serial only; 2 - mqtt only; 3 - both; negative values - silent mode (-1, -2, -3))\n");
            printf("\t-c config   \t\t\t config file\n");
            printf("\t-p PORT     \t\t\t set to what port wb-mqtt-serial should connect (default: 1883)\n");
            printf("\t-H IP       \t\t\t set to what IP wb-mqtt-serial should connect (default: localhost)\n");
            printf("\t-u USER     \t\t\t MQTT user (optional)\n");
            printf("\t-P PASSWORD \t\t\t MQTT user password (optional)\n");
            printf("\t-T prefix   \t\t\t MQTT topic prefix (optional)\n");

            exit(2);
        }
    }

    switch(debug) {
        case -1:
            Info.SetEnabled(false);
            break;

        case -2:
            WBMQTT::Info.SetEnabled(false);
            break;

        case -3:
            WBMQTT::Info.SetEnabled(false);
            Info.SetEnabled(false);
            break;

        case 1:
            Debug.SetEnabled(true);
            break;

        case 2:
            WBMQTT::Debug.SetEnabled(true);
            break;

        case 3:
            WBMQTT::Debug.SetEnabled(true);
            Debug.SetEnabled(true);
            break;

        default:
            break;
    }

    PHandlerConfig handlerConfig;
    try {
        TConfigTemplateParser deviceParser(templatesFolder, debug);
        TConfigParser parser(configFilename, debug, TSerialDeviceFactory::GetRegisterTypes,
                             deviceParser.Parse());
        handlerConfig = parser.Parse();
    } catch (const TConfigParserException& e) {
        Error.Log() << "[serial] FATAL: " << e.what();
        return 1;
    }

    if (mqttConfig.Id.empty())
        mqttConfig.Id = driverName;

    auto mqtt = WBMQTT::NewMosquittoMqttClient(mqttConfig);
    auto backend = WBMQTT::NewDriverBackend(mqtt);
    auto driver = WBMQTT::NewDriver(WBMQTT::TDriverArgs{}
        .SetId(driverName)
        .SetBackend(backend)
        .SetUseStorage(true)
        .SetReownUnknownDevices(true)
        .SetStoragePath(libwbmqttDbFile)
    );

    driver->StartLoop();
    WBMQTT::SignalHandling::OnSignal(SIGINT, [&]{ driver->StopLoop(); });

    driver->WaitForReady();

    try {
        auto serialDriver = make_shared<TMQTTSerialDriver>(driver, handlerConfig);

        if (serialDriver->WriteInitValues()) {
            Debug.Log() << "[serial] register-based setup performed.";
        }

        serialDriver->Start();

        WBMQTT::SignalHandling::OnSignal(SIGINT, [&]{ serialDriver->Stop(); });

        WBMQTT::SignalHandling::Start();
        WBMQTT::SignalHandling::Wait();
    } catch (const TSerialDeviceException & e) {
        Error.Log() << "[serial] FATAL: " << e.what();
        return 1;
    } catch (const WBMQTT::TBaseException & e) {
        Error.Log() << "[serial] FATAL: " << e.what();
        return 1;
    }

	return 0;
}
//build-dep libmosquittopp-dev libmosquitto-dev
// dep: libjsoncpp0 libmosquittopp libmosquitto

// TBD: fix race condition that occurs after modbus error on startup
// (slave not active)
// TBD: check json structure
// TBD: proper error checking everywhere (catch exceptions, etc.)
// TBD: automatic tests(?)
