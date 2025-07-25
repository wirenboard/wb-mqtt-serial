#include "log.h"
#include "serial_device.h"
#include "serial_driver.h"

#include <wblib/rpc.h>
#include <wblib/signal_handling.h>
#include <wblib/wbmqtt.h>

#include <filesystem>
#include <fstream>
#include <getopt.h>
#include <unistd.h>

#include "confed_config_generator.h"
#include "confed_json_generator.h"
#include "confed_schema_generator.h"
#include "config_schema_generator.h"

#include "device_template_generator.h"
#include "files_watcher.h"
#include "rpc/rpc_config.h"
#include "rpc/rpc_config_handler.h"
#include "rpc/rpc_device_handler.h"
#include "rpc/rpc_port_handler.h"
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
const auto PORTS_JSON_SCHEMA_FULL_FILE_PATH = "/usr/share/wb-mqtt-serial/wb-mqtt-serial-ports.schema.json";
const auto TEMPLATES_JSON_SCHEMA_FULL_FILE_PATH =
    "/usr/share/wb-mqtt-serial/wb-mqtt-serial-device-template.schema.json";
const auto RPC_PORT_LOAD_REQUEST_SCHEMA_FULL_FILE_PATH =
    "/usr/share/wb-mqtt-serial/wb-mqtt-serial-rpc-port-load-request.schema.json";
const auto RPC_PORT_SETUP_REQUEST_SCHEMA_FULL_FILE_PATH =
    "/usr/share/wb-mqtt-serial/wb-mqtt-serial-rpc-port-setup-request.schema.json";
const auto RPC_PORT_SCAN_REQUEST_SCHEMA_FULL_FILE_PATH =
    "/usr/share/wb-mqtt-serial/wb-mqtt-serial-rpc-port-scan-request.schema.json";
const auto RPC_DEVICE_LOAD_CONFIG_REQUEST_SCHEMA_FULL_FILE_PATH =
    "/usr/share/wb-mqtt-serial/wb-mqtt-serial-rpc-device-load-config-request.schema.json";
const auto RPC_DEVICE_PROBE_REQUEST_SCHEMA_FULL_FILE_PATH =
    "/usr/share/wb-mqtt-serial/wb-mqtt-serial-rpc-device-probe-request.schema.json";
const auto CONFED_COMMON_JSON_SCHEMA_FULL_FILE_PATH =
    "/usr/share/wb-mqtt-serial/wb-mqtt-serial-confed-common.schema.json";
const auto DEVICE_GROUP_NAMES_JSON_FULL_FILE_PATH = "/usr/share/wb-mqtt-serial/groups.json";
const auto PROTOCOL_SCHEMAS_DIR = "/usr/share/wb-mqtt-serial/protocols";

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
             << "  -d       level     enable debugging output:" << endl
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
             << "  -J                 Make /etc/wb-mqtt-serial.conf from wb-mqtt-confed output" << endl
             << "  -G       options   Generate device template. Type \"-G help\" for options description" << endl
             << "  -v                 Print the version" << endl;
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

    pair<shared_ptr<Json::Value>, PTemplateMap> LoadTemplates()
    {
        auto commonDeviceSchema =
            make_shared<Json::Value>(WBMQTT::JSON::Parse(CONFED_COMMON_JSON_SCHEMA_FULL_FILE_PATH));
        auto templates = make_shared<TTemplateMap>(
            LoadConfigTemplatesSchema(TEMPLATES_JSON_SCHEMA_FULL_FILE_PATH, *commonDeviceSchema));
        templates->AddTemplatesDir(TEMPLATES_DIR);
        templates->AddTemplatesDir(USER_TEMPLATES_DIR);
        return {commonDeviceSchema, templates};
    }

    void ConfedToConfig()
    {
        try {
            PTemplateMap templates;
            std::tie(std::ignore, templates) = LoadTemplates();
            MakeJsonWriter("  ", "None")->write(MakeConfigFromConfed(std::cin, *templates), &cout);
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

        while ((c = getopt(argc, argv, "d:c:h:H:p:u:P:T:jJG:v")) != -1) {
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
                    exit(EXIT_SUCCESS);
                case 'J': // make config JSON from confed's JSON
                    ConfedToConfig();
                    exit(EXIT_SUCCESS);
                case 'G':
                    GenerateDeviceTemplate(APP_NAME, USER_TEMPLATES_DIR, optarg);
                    exit(EXIT_SUCCESS);
                case 'v':
                    PrintStartupInfo();
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

    void HandleTemplateChangeEvent(TTemplateMap& templates,
                                   TDevicesConfedSchemasMap& confedSchemasMap,
                                   const std::string& fileName,
                                   TFilesWatcher::TEvent event)
    {
        if (event == TFilesWatcher::TEvent::CloseWrite) {
            LOG(Debug) << fileName << " changed. Reloading template";
            try {
                auto updatedTypes = templates.UpdateTemplate(fileName);
                for (const auto& deviceType: updatedTypes) {
                    confedSchemasMap.InvalidateCache(deviceType);
                }
            } catch (const exception& e) {
                LOG(Error) << "Failed to reload template: " << e.what();
            }
            return;
        }
        if (event == TFilesWatcher::TEvent::Delete) {
            LOG(Debug) << fileName << " deleted";
            confedSchemasMap.InvalidateCache(templates.DeleteTemplate(fileName));
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

    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    shared_ptr<Json::Value> commonDeviceSchema;
    PTemplateMap templates;
    std::tie(commonDeviceSchema, templates) = LoadTemplates();
    TDevicesConfedSchemasMap confedSchemasMap(*templates, deviceFactory, *commonDeviceSchema);
    TProtocolConfedSchemasMap protocolSchemasMap(PROTOCOL_SCHEMAS_DIR, *commonDeviceSchema);
    auto portsSchema = WBMQTT::JSON::Parse(PORTS_JSON_SCHEMA_FULL_FILE_PATH);

    TFilesWatcher watcher({TEMPLATES_DIR, USER_TEMPLATES_DIR}, [&](std::string fileName, TFilesWatcher::TEvent event) {
        HandleTemplateChangeEvent(*templates, confedSchemasMap, fileName, event);
    });

    try {
        if (mqttConfig.Id.empty())
            mqttConfig.Id = driverName;

        auto mqtt = WBMQTT::NewMosquittoMqttClient(mqttConfig);

        auto rpcServer(WBMQTT::NewMqttRpcServer(mqtt, APP_NAME));

        TRPCConfigHandler rpcConfigHandler(configFilename,
                                           portsSchema,
                                           templates,
                                           confedSchemasMap,
                                           protocolSchemasMap,
                                           WBMQTT::JSON::Parse(DEVICE_GROUP_NAMES_JSON_FULL_FILE_PATH),
                                           rpcServer);

        PRPCConfig rpcConfig = std::make_shared<TRPCConfig>();
        PHandlerConfig handlerConfig;

        try {
            handlerConfig = LoadConfig(configFilename,
                                       deviceFactory,
                                       *commonDeviceSchema,
                                       *templates,
                                       rpcConfig,
                                       portsSchema,
                                       protocolSchemasMap);
        } catch (const exception& e) {
            LOG(Error) << e.what();
        }

        PMQTTSerialDriver serialDriver;
        TRPCDeviceParametersCache parametersCache;

        if (handlerConfig) {
            if (handlerConfig->Debug) {
                Debug.SetEnabled(true);
            }

            auto backend = WBMQTT::NewDriverBackend(mqtt);
            auto driver = WBMQTT::NewDriver(WBMQTT::TDriverArgs{}
                                                .SetId(driverName)
                                                .SetBackend(backend)
                                                .SetUseStorage(true)
                                                .SetReownUnknownDevices(true)
                                                .SetStoragePath(LIBWBMQTT_DB_FULL_FILE_PATH));

            driver->StartLoop();
            WBMQTT::SignalHandling::OnSignals({SIGINT, SIGTERM}, [=] {
                driver->StopLoop();
                driver->Close();
            });

            driver->WaitForReady();
            serialDriver = make_shared<TMQTTSerialDriver>(driver, handlerConfig);
            parametersCache.RegisterCallbacks(handlerConfig);
        }

        TSerialClientTaskRunner serialClientTaskRunner(serialDriver);
        auto rpcPortHandler = std::make_shared<TRPCPortHandler>(RPC_PORT_LOAD_REQUEST_SCHEMA_FULL_FILE_PATH,
                                                                RPC_PORT_SETUP_REQUEST_SCHEMA_FULL_FILE_PATH,
                                                                RPC_PORT_SCAN_REQUEST_SCHEMA_FULL_FILE_PATH,
                                                                rpcConfig,
                                                                serialClientTaskRunner,
                                                                parametersCache,
                                                                rpcServer);
        auto rpcDeviceHandler =
            std::make_shared<TRPCDeviceHandler>(RPC_DEVICE_LOAD_CONFIG_REQUEST_SCHEMA_FULL_FILE_PATH,
                                                RPC_DEVICE_PROBE_REQUEST_SCHEMA_FULL_FILE_PATH,
                                                deviceFactory,
                                                templates,
                                                serialClientTaskRunner,
                                                parametersCache,
                                                rpcServer);

        if (serialDriver) {
            serialDriver->Start();
        } else {
            mqtt->Start();
        }
        rpcServer->Start();

        WBMQTT::SignalHandling::OnSignals({SIGINT, SIGTERM}, [=] {
            rpcServer->Stop();
            if (serialDriver) {
                serialDriver->Stop();
            } else {
                mqtt->Stop();
            }
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
