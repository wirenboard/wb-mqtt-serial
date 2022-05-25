#include "log.h"
#include "serial_device.h"
#include "serial_driver.h"

#include <wblib/rpc.h>
#include <wblib/signal_handling.h>
#include <wblib/wbmqtt.h>

#include <experimental/filesystem>
#include <fstream>
#include <getopt.h>
#include <unistd.h>

#include "confed_config_generator.h"
#include "confed_json_generator.h"
#include "confed_schema_generator.h"
#include "config_schema_generator.h"

#include "device_template_generator.h"
#include "serial_port.h"

#define STR(x) #x
#define XSTR(x) STR(x)

using namespace std;

#define LOG(logger) ::logger.Log() << "[serial] "

const auto driverName = "wb-modbus";

const auto APP_NAME = "wb-mqtt-serial";

const auto LIBWBMQTT_DB_FULL_FILE_PATH = "/var/lib/wb-mqtt-serial/libwbmqtt.db";
const auto CONFIG_FULL_FILE_PATH = "/etc/wb-mqtt-serial.conf";
const auto TEMPLATES_DIR = "/usr/share/wb-mqtt-serial/templates";
const auto USER_TEMPLATES_DIR = "/etc/wb-mqtt-serial.conf.d/templates";
const auto CONFIG_JSON_SCHEMA_FULL_FILE_PATH = "/usr/share/wb-mqtt-serial/wb-mqtt-serial.schema.json";
const auto TEMPLATES_JSON_SCHEMA_FULL_FILE_PATH =
    "/usr/share/wb-mqtt-serial/wb-mqtt-serial-device-template.schema.json";
const auto CONFED_JSON_SCHEMA_FULL_FILE_PATH = "/var/lib/wb-mqtt-confed/schemas/wb-mqtt-serial.schema.json";

const auto SERIAL_DRIVER_STOP_TIMEOUT_S = chrono::seconds(60);

namespace
{
    void PrintStartupInfo()
    {
        string commit(XSTR(WBMQTT_COMMIT));
        cout << APP_NAME << " " << XSTR(WBMQTT_VERSION);
        if (!commit.empty()) {
            cout << " git " << commit;
        }
        cout << endl;
    }

    void PrintUsage()
    {
        cout << "Usage:" << endl
             << " " << APP_NAME << " [options]" << endl
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
             << "  -T       prefix    MQTT topic prefix (optional)" << endl
             << "  -g                 Generate JSON Schema for wb-mqtt-confed" << endl
             << "  -j                 Make JSON for wb-mqtt-confed from /etc/wb-mqtt-serial.conf" << endl
             << "  -J                 Make /etc/wb-mqtt-serial.conf from wb-mqtt-confed output" << endl
             << "  -G       options   Generate device template. Type \"-G help\" for options description" << endl;
    }

    /**
     * @brief Create Json::StreamWriter with defined parameters
     *
     * @param indentation - a string that is added before lines as indentation, "" - no indentation
     * @param commentStyle - "All" (write comments) or "None" (do not write comments)
     */
    unique_ptr<Json::StreamWriter> MakeJsonWriter(const std::string& indentation, const std::string& commentStyle)
    {
        Json::StreamWriterBuilder builder;
        builder["commentStyle"] = commentStyle;
        builder["indentation"] = indentation;
        builder["precision"] = 15;
        return unique_ptr<Json::StreamWriter>(builder.newStreamWriter());
    }

    pair<shared_ptr<Json::Value>, shared_ptr<TTemplateMap>> LoadTemplates()
    {
        auto configSchema = make_shared<Json::Value>(std::move(LoadConfigSchema(CONFIG_JSON_SCHEMA_FULL_FILE_PATH)));
        auto templates =
            make_shared<TTemplateMap>(TEMPLATES_DIR,
                                      LoadConfigTemplatesSchema(TEMPLATES_JSON_SCHEMA_FULL_FILE_PATH, *configSchema));
        try {
            templates->AddTemplatesDir(USER_TEMPLATES_DIR); // User templates dir
        } catch (const TConfigParserException& e) {
        } // Pass exception if user templates dir doesn't exist
        return {configSchema, templates};
    }

    void ConfigToConfed()
    {
        try {
            TSerialDeviceFactory deviceFactory;
            RegisterProtocols(deviceFactory);
            shared_ptr<Json::Value> configSchema;
            shared_ptr<TTemplateMap> templates;
            std::tie(configSchema, templates) = LoadTemplates();
            MakeJsonWriter("", "None")
                ->write(MakeJsonForConfed(CONFIG_FULL_FILE_PATH, *configSchema, *templates, deviceFactory), &cout);
        } catch (const exception& e) {
            LOG(Error) << e.what();
            exit(EXIT_FAILURE);
        }
    }

    void ConfedToConfig()
    {
        try {
            shared_ptr<TTemplateMap> templates;
            std::tie(std::ignore, templates) = LoadTemplates();
            MakeJsonWriter("  ", "None")->write(MakeConfigFromConfed(std::cin, *templates), &cout);
        } catch (const exception& e) {
            LOG(Error) << e.what();
            exit(EXIT_FAILURE);
        }
    }

    void SchemaForConfed()
    {
        try {
            TSerialDeviceFactory deviceFactory;
            RegisterProtocols(deviceFactory);
            shared_ptr<Json::Value> configSchema;
            shared_ptr<TTemplateMap> templates;
            std::tie(configSchema, templates) = LoadTemplates();
            const char* resultingSchemaFile = "/tmp/wb-mqtt-serial.schema.json";
            {
                ofstream f(resultingSchemaFile);
                MakeJsonWriter(" ", "All")->write(MakeSchemaForConfed(*configSchema, *templates, deviceFactory), &f);
            }
            ifstream src(resultingSchemaFile, ios::binary);
            experimental::filesystem::path file(CONFED_JSON_SCHEMA_FULL_FILE_PATH);
            experimental::filesystem::create_directories(file.parent_path());
            ofstream dst(file, ios::binary);
            dst << src.rdbuf();
        } catch (const exception& e) {
            LOG(Error) << e.what();
            exit(EXIT_FAILURE);
        }
    }

    void SetDebugLevel(const char* optarg)
    {
        try {
            auto debugLevel = stoi(optarg);
            switch (debugLevel) {
                case 0:
                    return;
                case -1:
                    Info.SetEnabled(false);
                    return;

                case -2:
                    WBMQTT::Info.SetEnabled(false);
                    return;

                case -3:
                    WBMQTT::Info.SetEnabled(false);
                    Info.SetEnabled(false);
                    return;

                case 1:
                    Debug.SetEnabled(true);
                    return;

                case 2:
                    WBMQTT::Debug.SetEnabled(true);
                    return;

                case 3:
                    WBMQTT::Debug.SetEnabled(true);
                    Debug.SetEnabled(true);
                    return;
            }
        } catch (...) {
        }
        cout << "Invalid -d parameter value " << optarg << endl;
        PrintUsage();
        exit(2);
    }

    void ParseCommadLine(int argc, char* argv[], WBMQTT::TMosquittoMqttConfig& mqttConfig, string& customConfig)
    {
        int c;

        while ((c = getopt(argc, argv, "d:c:h:H:p:u:P:T:jJgG:")) != -1) {
            switch (c) {
                case 'd':
                    SetDebugLevel(optarg);
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
                case 'j': // make JSON for confed from config's JSON
                    ConfigToConfed();
                    exit(EXIT_SUCCESS);
                case 'J': // make config JSON from confed's JSON
                    ConfedToConfig();
                    exit(EXIT_SUCCESS);
                case 'g':
                    SchemaForConfed();
                    exit(EXIT_SUCCESS);
                case 'G':
                    GenerateDeviceTemplate(APP_NAME, USER_TEMPLATES_DIR, optarg);
                    exit(EXIT_SUCCESS);
                case '?':
                default:
                    PrintStartupInfo();
                    PrintUsage();
                    exit(2);
            }
        }

        if (optind < argc) {
            for (int index = optind; index < argc; ++index) {
                cout << "Skipping unknown argument " << argv[index] << endl;
            }
        }
    }
}

int main(int argc, char* argv[])
{
    WBMQTT::TMosquittoMqttConfig mqttConfig;
    string configFilename(CONFIG_FULL_FILE_PATH);

    WBMQTT::SignalHandling::Handle({SIGINT, SIGTERM});
    WBMQTT::SignalHandling::OnSignals({SIGINT, SIGTERM}, [&] { WBMQTT::SignalHandling::Stop(); });
    WBMQTT::SetThreadName(APP_NAME);

    ParseCommadLine(argc, argv, mqttConfig, configFilename);

    PHandlerConfig handlerConfig;
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);
    try {
        Json::Value configSchema = LoadConfigSchema(CONFIG_JSON_SCHEMA_FULL_FILE_PATH);
        TTemplateMap templates(TEMPLATES_DIR,
                               LoadConfigTemplatesSchema(TEMPLATES_JSON_SCHEMA_FULL_FILE_PATH, configSchema));

        try {
            templates.AddTemplatesDir(USER_TEMPLATES_DIR); // User templates dir
        } catch (const TConfigParserException& e) {        // Pass exception if user templates dir doesn't exist
        }

        handlerConfig = LoadConfig(configFilename, deviceFactory, configSchema, templates);
    } catch (const exception& e) {
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
                                            .SetStoragePath(LIBWBMQTT_DB_FULL_FILE_PATH),
                                        handlerConfig->PublishParameters);

        auto rpcServer(WBMQTT::NewMqttRpcServer(mqtt, APP_NAME));

        driver->StartLoop();
        WBMQTT::SignalHandling::OnSignals({SIGINT, SIGTERM}, [&] {
            driver->StopLoop();
            driver->Close();
        });

        driver->WaitForReady();

        auto serialDriver = make_shared<TMQTTSerialDriver>(driver, handlerConfig, rpcServer);

        serialDriver->Start();
        rpcServer->Start();

        WBMQTT::SignalHandling::OnSignals({SIGINT, SIGTERM}, [&] {
            rpcServer->Stop();
            serialDriver->Stop();
        });
        WBMQTT::SignalHandling::SetOnTimeout(SERIAL_DRIVER_STOP_TIMEOUT_S, [&] {
            LOG(Error) << "Driver takes too long to stop. Exiting.";
            exit(EXIT_FAILURE);
        });
        WBMQTT::SignalHandling::Start();
        WBMQTT::SignalHandling::Wait();
    } catch (const exception& e) {
        LOG(Error) << "FATAL: " << e.what();
        return 1;
    }
    return 0;
}
// TBD: fix race condition that occurs after modbus error on startup
// (slave not active)
// TBD: proper error checking everywhere (catch exceptions, etc.)
