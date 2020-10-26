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

const auto BIN_NAME        = "wb-mqtt-serial";

const auto LIBWBMQTT_DB_FULL_FILE_PATH          = "/var/lib/wb-mqtt-serial/libwbmqtt.db";
const auto CONFIG_FULL_FILE_PATH                = "/etc/wb-mqtt-serial.conf";
const auto TEMPLATES_DIR                        = "/usr/share/wb-mqtt-serial/templates";
const auto USER_TEMPLATES_DIR                   = "/etc/wb-mqtt-serial.conf.d/templates";
const auto CONFIG_JSON_SCHEMA_FULL_FILE_PATH    = "/usr/share/wb-mqtt-serial/wb-mqtt-serial.schema.json";
const auto TEMPLATES_JSON_SCHEMA_FULL_FILE_PATH = "/usr/share/wb-mqtt-serial/wb-mqtt-serial-device-template.schema.json";

const auto SERIAL_DRIVER_STOP_TIMEOUT_S = chrono::seconds(3);

namespace
{
    void PrintUsage()
    {
        cout << "Usage:" << endl
             << " " << BIN_NAME << " [options]" << endl
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
    string configFilename(CONFIG_FULL_FILE_PATH);

    WBMQTT::SignalHandling::Handle({ SIGINT });
    WBMQTT::SignalHandling::OnSignal(SIGINT, [&]{ WBMQTT::SignalHandling::Stop(); });
    WBMQTT::SetThreadName(BIN_NAME);

    ParseCommadLine(argc, argv, mqttConfig, configFilename);

    PHandlerConfig handlerConfig;
    try {
        Json::Value configSchema = LoadConfigSchema(CONFIG_JSON_SCHEMA_FULL_FILE_PATH);
        TTemplateMap templates(TEMPLATES_DIR,
                               LoadConfigTemplatesSchema(TEMPLATES_JSON_SCHEMA_FULL_FILE_PATH, 
                                                         configSchema));

        try {
            templates.AddTemplatesDir(USER_TEMPLATES_DIR); // User templates dir
        } catch (const TConfigParserException& e) {        // Pass exception if user templates dir doesn't exist
        }

        handlerConfig = LoadConfig(configFilename,
                                  TSerialDeviceFactory::GetRegisterTypes,
                                  configSchema,
                                  templates);
    } catch (const std::exception& e) {
        LOG(Error) << e.what();
        return 0;
    }

    try {
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
            .SetStoragePath(LIBWBMQTT_DB_FULL_FILE_PATH)
        );

        driver->StartLoop();
        WBMQTT::SignalHandling::OnSignal(SIGINT, [&]{ driver->StopLoop(); });

        driver->WaitForReady();

        auto serialDriver = make_shared<TMQTTSerialDriver>(driver, handlerConfig);

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
