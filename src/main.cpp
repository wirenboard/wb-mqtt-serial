#include "serial_device.h"
#include "serial_driver.h"
#include "log.h"

#include <wblib/wbmqtt.h>
#include <wblib/signal_handling.h>

#include <getopt.h>
#include <unistd.h>
#include <fstream>

#include "confed_schema_generator.h"
#include "confed_json_generator.h"
#include "confed_config_generator.h"

#include "devices/dlms_device.h"
#include "serial_port.h"

#define STR(x) #x
#define XSTR(x) STR(x)

using namespace std;

#define LOG(logger) ::logger.Log() << "[serial] "

const auto driverName      = "wb-modbus";

const auto APP_NAME        = "wb-mqtt-serial";

const auto LIBWBMQTT_DB_FULL_FILE_PATH          = "/var/lib/wb-mqtt-serial/libwbmqtt.db";
const auto CONFIG_FULL_FILE_PATH                = "/etc/wb-mqtt-serial.conf";
const auto TEMPLATES_DIR                        = "/usr/share/wb-mqtt-serial/templates";
const auto USER_TEMPLATES_DIR                   = "/etc/wb-mqtt-serial.conf.d/templates";
const auto CONFIG_JSON_SCHEMA_FULL_FILE_PATH    = "/usr/share/wb-mqtt-serial/wb-mqtt-serial.schema.json";
const auto OBIS_CODE_HINTS_FULL_FILE_PATH       = "/usr/share/wb-mqtt-serial/obis-hints.json";
const auto TEMPLATES_JSON_SCHEMA_FULL_FILE_PATH = "/usr/share/wb-mqtt-serial/wb-mqtt-serial-device-template.schema.json";

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

    void PrintDenerateTemplateUsage()
    {
        cout << "Usage:" << endl
             << " " << APP_NAME << " -G mode,path_to_device,port_settings,protocol[:device_id][,options]" << endl
             << "Options:" << endl
             << "  mode            generation mode:" << endl
             << "                    0 - minimal information, no actual template generation" << endl
             << "                    1 - extended information, no actual template generation" << endl
             << "                    2 - template generation to /etc/wb-mqtt-serial.conf.d/templates" << endl
             << "  path_to_device  path to device's file (example: /dev/ttyRS485-1)" << endl
             << "  port_settings   baudrate, data bits, parity, stop bits, separated by '-' (example: 9600-8-N-1)" << endl
             << "  protocol        one of:" << endl
             << "                    dlms_hdlc - DLMS/COSEM with HDLC interface," << endl
             << "                                optional device_id is an address of a Physical Device" << endl
             << "  options         comma-separated list of protocol parameters:" << endl
             << "dlms_hdlc protocol options:" << endl
             << "  - client address" << endl
             << "  - authentication mechanism:" << endl
             << "     0 - lowest" << endl
             << "     1 - low" << endl
             << "     2 - high" << endl
             << "     3 - high using MD5" << endl
             << "     4 - high using SHA1" << endl
             << "     5 - high using GMAC" << endl
             << "     6 - high using SHA256" << endl
             << "     7 - high using ECDSA" << endl
             << "  - password" << endl;
    }

    void ConfigToConfed()
    {
        try {
            Json::Value configSchema = LoadConfigSchema(CONFIG_JSON_SCHEMA_FULL_FILE_PATH);
            TTemplateMap templates(TEMPLATES_DIR,
                                    LoadConfigTemplatesSchema(TEMPLATES_JSON_SCHEMA_FULL_FILE_PATH, configSchema));
            try {
                templates.AddTemplatesDir(USER_TEMPLATES_DIR); // User templates dir
            } catch (const TConfigParserException& e) {        // Pass exception if user templates dir doesn't exist
            }
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "";
            unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
            writer->write(MakeJsonForConfed(CONFIG_FULL_FILE_PATH, configSchema, templates), &cout);
        } catch (const exception& e) {
            cout << e.what() << endl;
        }
    }

    void ConfedToConfig()
    {
        try {
            Json::Value configSchema = LoadConfigSchema(CONFIG_JSON_SCHEMA_FULL_FILE_PATH);
            TTemplateMap templates(TEMPLATES_DIR,
                                    LoadConfigTemplatesSchema(TEMPLATES_JSON_SCHEMA_FULL_FILE_PATH, configSchema));
            try {
                templates.AddTemplatesDir(USER_TEMPLATES_DIR); // User templates dir
            } catch (const TConfigParserException& e) {        // Pass exception if user templates dir doesn't exist
            }
            Json::StreamWriterBuilder builder;
            builder["indentation"] = "    ";
            unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
            writer->write(MakeConfigFromConfed(std::cin, templates), &cout);
        } catch (const exception& e) {
            cout << e.what() << endl;
        }
    }

    void SchemaForConfed()
    {
        try {
            Json::Value configSchema = LoadConfigSchema(CONFIG_JSON_SCHEMA_FULL_FILE_PATH);
            TTemplateMap templates(TEMPLATES_DIR,
                                LoadConfigTemplatesSchema(TEMPLATES_JSON_SCHEMA_FULL_FILE_PATH, 
                                                            configSchema));

            try {
                templates.AddTemplatesDir(USER_TEMPLATES_DIR); // User templates dir
            } catch (const TConfigParserException& e) {        // Pass exception if user templates dir doesn't exist
            }

            TSerialDeviceFactory deviceFactory;
            RegisterProtocols(deviceFactory);

            Json::StreamWriterBuilder builder;
            builder["indentation"] = "    ";
            unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
            const char* resultingSchemaFile = "/tmp/wb-mqtt-serial.schema.json";
            {
                ofstream f(resultingSchemaFile);
                MakeSchemaForConfed(configSchema, templates, deviceFactory);
                writer->write(configSchema, &f);
            }
            ifstream  src(resultingSchemaFile, ios::binary);
            ofstream  dst("/usr/share/wb-mqtt-confed/schemas/wb-mqtt-serial.schema.json", ios::binary);
            dst << src.rdbuf();
        } catch (const exception& e) {
            LOG(Error) << e.what();
        }
    }

    TObisCodeHints LoadObisCodeHints()
    {
        TObisCodeHints res;
        try {
            for (auto& c: WBMQTT::JSON::Parse(OBIS_CODE_HINTS_FULL_FILE_PATH)) {
                if (c.isMember("obis")) {
                        res.insert({c["obis"].asString(), {c["description"].asString(), c["control"].asString()}});
                }
            }
        } catch (const std::exception& e) {
            LOG(Warn) << e.what();
        }
        return res;
    }

    void GenerateDeviceTemplate(const char* options)
    {
        std::vector<std::function<void(const std::string&)>> handlers;
        if (strncmp(options, "help", 4) == 0) {
            PrintStartupInfo();
            PrintDenerateTemplateUsage();
            return;
        }
        auto params = WBMQTT::StringSplit(options, ",");
        if (params.size() < 3) {
            cout << "Not enough options" << endl;
            PrintDenerateTemplateUsage();
            return;
        }
        try {
            TSerialPortSettings portSettings;
            auto deviceConfig = std::make_shared<TDlmsDeviceConfig>();
            PPort port;
            uint32_t mode;

            handlers.push_back([&](const std::string& str) {
                mode = atoi(str.c_str());
                if (mode < 0 || mode > 2) {
                    throw std::runtime_error("Generation mode must be in range [0, 1, 2]");
                }
            });

            handlers.push_back([&](const std::string& str) {
                portSettings.Device = str;
            });

            handlers.push_back([&](const std::string& str) {
                auto portParams = WBMQTT::StringSplit(str, "-");
                portSettings.BaudRate = atoi(portParams[0].c_str());
                portSettings.DataBits = atoi(portParams[1].c_str());
                portSettings.Parity = portParams[2][0];
                portSettings.StopBits = atoi(portParams[3].c_str());
                port = std::make_shared<TSerialPort>(portSettings);
            });

            std::string phisycalDeviceAddress;

            handlers.push_back([&](const std::string& str) {
                auto protocolParams = WBMQTT::StringSplit(str, ":");
                phisycalDeviceAddress = (protocolParams.size() == 1) ? std::string() : protocolParams[1];
            });

            handlers.push_back([&](const std::string& str) {
                deviceConfig->ClientAddress = atoi(str.c_str());
            });

            handlers.push_back([&](const std::string& str) {
                switch (str[0])
                {
                    case '1': deviceConfig->Authentication = DLMS_AUTHENTICATION_LOW; break;
                    case '2': deviceConfig->Authentication = DLMS_AUTHENTICATION_HIGH; break;
                    case '3': deviceConfig->Authentication = DLMS_AUTHENTICATION_HIGH_MD5; break;
                    case '4': deviceConfig->Authentication = DLMS_AUTHENTICATION_HIGH_SHA1; break;
                    case '5': deviceConfig->Authentication = DLMS_AUTHENTICATION_HIGH_GMAC; break;
                    case '6': deviceConfig->Authentication = DLMS_AUTHENTICATION_HIGH_SHA256; break;
                    case '7': deviceConfig->Authentication = DLMS_AUTHENTICATION_HIGH_ECDSA; break;
                }
            });

            handlers.push_back([&](const std::string& str) {
                deviceConfig->Password.insert(deviceConfig->Password.begin(), str.begin(), str.end());
            });

            auto handlersIt = handlers.begin();
            auto paramsIt = params.begin();
            while (paramsIt != params.end() || handlersIt != handlers.end()) {
                (*handlersIt)(*paramsIt);
                ++paramsIt;
                ++handlersIt;
            }

            deviceConfig->SlaveId = phisycalDeviceAddress;
            deviceConfig->ResponseTimeout = std::chrono::milliseconds(1000);
            deviceConfig->FrameTimeout = std::chrono::milliseconds(20);

            TSerialDeviceFactory deviceFactory;
            TDlmsDevice::Register(deviceFactory);
            port->Open();
            TDlmsDevice device(deviceConfig, port, deviceFactory.GetProtocol("dlms"));
            std::cout << "Getting logical devices..." << std::endl;
            auto logicalDevices = device.GetLogicalDevices();
            std::cout << "Logical devices:" << std::endl;
            for (const auto& ld: logicalDevices) {
                std::cout << ld.first << ": " << ld.second << std::endl;
            }
            std::cout << std::endl;
            for (const auto& ld: logicalDevices) {
                deviceConfig->LogicalObjectAddress = ld.first;
                TDlmsDevice d(deviceConfig, port, deviceFactory.GetProtocol("dlms"));
                auto objs = d.ReadAllObjects(mode != 0);
                TObisCodeHints obisHints = LoadObisCodeHints();
                switch (mode) {
                    case 0: Print(objs, false, obisHints); break;
                    case 1: Print(objs, true, obisHints); break;
                    case 2: 
                        Json::StreamWriterBuilder builder;
                        builder["indentation"] = "  ";
                        unique_ptr<Json::StreamWriter> writer(builder.newStreamWriter());
                        auto templateFile = std::string(USER_TEMPLATES_DIR) + "/" + ld.second + ".conf";
                        ofstream f(templateFile);
                        writer->write(GenerateDeviceTemplate(ld.second, deviceConfig->Authentication, objs, obisHints), &f);
                        std::cout << templateFile << " is generated" << std::endl;
                }
            }
        } catch (const exception& e) {
            cout << "[ERROR]" << e.what() << endl;
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
        } catch (...) {}
        cout << "Invalid -d parameter value " << optarg << endl;
        PrintUsage();
        exit(2);
    }

    void ParseCommadLine(int                           argc,
                         char*                         argv[],
                         WBMQTT::TMosquittoMqttConfig& mqttConfig,
                         string&                       customConfig)
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
                exit(0);
            case 'J': // make config JSON from confed's JSON
                ConfedToConfig();
                exit(0);
            case 'g':
                SchemaForConfed();
                exit(0);
            case 'G':
                GenerateDeviceTemplate(optarg);
                exit(0);
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

int main(int argc, char *argv[])
{
    WBMQTT::TMosquittoMqttConfig mqttConfig;
    string configFilename(CONFIG_FULL_FILE_PATH);

    WBMQTT::SignalHandling::Handle({ SIGINT, SIGTERM });
    WBMQTT::SignalHandling::OnSignals( {SIGINT, SIGTERM }, [&]{ WBMQTT::SignalHandling::Stop(); });
    WBMQTT::SetThreadName(APP_NAME);

    ParseCommadLine(argc, argv, mqttConfig, configFilename);

    PHandlerConfig handlerConfig;
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);
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
                                  deviceFactory,
                                  configSchema,
                                  templates);
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
            handlerConfig->PublishParameters
        );

        driver->StartLoop();
        WBMQTT::SignalHandling::OnSignals({SIGINT, SIGTERM}, 
                                          [&]{
                                                driver->StopLoop();
                                                driver->Close();
                                            });

        driver->WaitForReady();

        auto serialDriver = make_shared<TMQTTSerialDriver>(driver, handlerConfig);

        serialDriver->Start();

        WBMQTT::SignalHandling::OnSignals({ SIGINT, SIGTERM }, [&]{ serialDriver->Stop(); });
        WBMQTT::SignalHandling::SetOnTimeout(SERIAL_DRIVER_STOP_TIMEOUT_S, [&]{
            LOG(Error) << "Driver takes too long to stop. Exiting.";
            exit(1);
        });
        WBMQTT::SignalHandling::Start();
        WBMQTT::SignalHandling::Wait();
    } catch (const exception & e) {
        LOG(Error) << "FATAL: " << e.what();
        return 1;
    }
	return 0;
}
// TBD: fix race condition that occurs after modbus error on startup
// (slave not active)
// TBD: proper error checking everywhere (catch exceptions, etc.)
