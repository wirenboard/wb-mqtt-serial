#include "serial_device.h"
#include "serial_driver.h"
#include "log.h"

#include <wblib/wbmqtt.h>
#include <wblib/signal_handling.h>

#include <getopt.h>
#include <unistd.h>

using namespace std;

#define LOG(logger) ::logger.Log() << "[serial] "

const auto driverName      = "wb-modbus";
const auto libwbmqttDbFile = "/var/lib/wb-mqtt-serial/libwbmqtt.db";
const auto templatesFolder = "/usr/share/wb-mqtt-serial/templates";
const auto SERIAL_DRIVER_STOP_TIMEOUT_S = chrono::seconds(3);

namespace
{
    void PrintUsage()
    {
        cout << "Usage:" << endl
             << " wb-mqtt-serial [options]" << endl
             << "Options:" << endl
             << "  -d       level     enable debuging output:" << endl
             << "                       1 - serial only;" << endl
             << "                       2 - mqtt only;" << endl
             << "                       3 - both;" << endl
             << "                       negative values - silent mode (-1, -2, -3))" << endl
             << "  -c       config    config file" << endl
             << "  -p       port      MQTT broker port (default: 1883)" << endl
             << "  -h, -H   IP        MQTT broker IP (default: localhost)" << endl
             << "  -u       user      MQTT user (optional)" << endl
             << "  -P       password  MQTT user password (optional)" << endl
             << "  -T       prefix    MQTT topic prefix (optional)" << endl;
    }

    void ParseCommadLine(int                           argc,
                         char*                         argv[],
                         WBMQTT::TMosquittoMqttConfig& mqttConfig,
                         string&                       customConfig)
    {
        int debugLevel = 0;
        int c;

        while ((c = getopt(argc, argv, "d:c:h:H:p:u:P:T:")) != -1) {
            switch (c) {
            case 'd':
                debugLevel = stoi(optarg);
                break;
            case 'c':
                customConfig = optarg;
                break;
            case 'p':
                mqttConfig.Port = stoi(optarg);
                break;
            case 'h':
            case 'H': // backward compatibility
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
                PrintUsage();
                exit(2);
            }
        }

        switch (debugLevel) {
        case 0:
            break;
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
            cout << "Invalid -d parameter value " << debugLevel << endl;
            PrintUsage();
            exit(2);
        }

        if (optind < argc) {
            for (int index = optind; index < argc; ++index) {
                cout << "Skipping unknown argument " << argv[index] << endl;
            }
        }
    }
}

int main(int argc, char *argv[])
{
    WBMQTT::TMosquittoMqttConfig mqttConfig;
    string configFilename;

    WBMQTT::SignalHandling::Handle({ SIGINT });
    WBMQTT::SignalHandling::OnSignal(SIGINT, [&]{ WBMQTT::SignalHandling::Stop(); });
    WBMQTT::SetThreadName("main");

    ParseCommadLine(argc, argv, mqttConfig, configFilename);

    PHandlerConfig handlerConfig;
    try {
        Json::Value configSchema = LoadConfigSchema("/usr/share/wb-mqtt-serial/wb-mqtt-serial.schema.json");
        TTemplateMap templates(templatesFolder, 
                               LoadConfigTemplatesSchema("/usr/share/wb-mqtt-serial/wb-mqtt-serial-device-template.schema.json", 
                                                         configSchema));
        handlerConfig = LoadConfig(configFilename,
                                  TSerialDeviceFactory::GetRegisterTypes,
                                  configSchema,
                                  templates);
    } catch (const std::exception& e) {
        LOG(Error) << "FATAL: " << e.what();
        return 1;
    }

    if (handlerConfig->Debug)
        Debug.SetEnabled(true);

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
            LOG(Debug) << "register-based setup performed.";
        }

        serialDriver->Start();

        WBMQTT::SignalHandling::OnSignal(SIGINT, [&]{ serialDriver->Stop(); });
        WBMQTT::SignalHandling::SetOnTimeout(SERIAL_DRIVER_STOP_TIMEOUT_S, [&]{
            LOG(Error) << "Driver takes too long to stop. Exiting.";
            exit(1);
        });
        WBMQTT::SignalHandling::Start();
        WBMQTT::SignalHandling::Wait();
    } catch (const std::exception & e) {
        LOG(Error) << "FATAL: " << e.what();
        return 1;
    }
	return 0;
}
// TBD: fix race condition that occurs after modbus error on startup
// (slave not active)
// TBD: proper error checking everywhere (catch exceptions, etc.)
