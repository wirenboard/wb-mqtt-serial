#include "device_template_generator.h"

#include <string.h>
#include <wblib/utils.h>

#include "devices/dlms_device.h"
#include "log.h"
#include "port/serial_port.h"

using namespace std;

namespace
{
    const auto REQUIRED_PARAMS_COUNT = 4;

    void PrintDeviceTemplateGenerationOptionsUsage(const std::string& appName)
    {
        cout << "Usage:" << endl
             << " " << appName << " -G mode,path_to_device,port_settings,protocol:device_id[,options]" << endl
             << "Options:" << endl
             << "  mode            generation mode:" << endl
             << "                    print     - minimal information, no actual template generation" << endl
             << "                    print_all - extended information, no actual template generation" << endl
             << "                    generate  - template generation to /etc/wb-mqtt-serial.conf.d/templates" << endl
             << "  path_to_device  path to device's file (example: /dev/ttyRS485-1)" << endl
             << "  port_settings   baudrate, data bits, parity, stop bits, separated by '-' (example: 9600-8-N-1)"
             << endl
             << "  protocol        one of:" << endl
             << "                    dlms_hdlc - DLMS/COSEM with HDLC interface," << endl
             << "                                device_id is an address of a Physical Device" << endl
             << "  options         comma-separated list of protocol parameters:" << endl;
        DLMS::PrintDeviceTemplateGenerationOptionsUsage();
    }
}

void GenerateDeviceTemplate(const std::string& appName, const std::string& destinationDir, const char* options)
{
    if (strncmp(options, "help", 4) == 0) {
        PrintDeviceTemplateGenerationOptionsUsage(appName);
        return;
    }

    auto params = WBMQTT::StringSplit(options, ",");
    if (params.size() < REQUIRED_PARAMS_COUNT) {
        Error.Log() << "Not enough options";
        PrintDeviceTemplateGenerationOptionsUsage(appName);
        return;
    }

    try {
        const std::unordered_map<std::string, TDeviceTemplateGenerationMode> generationModes = {
            {"print", PRINT},
            {"print_all", VERBOSE_PRINT},
            {"generate", GENERATE_TEMPLATE}};

        TDeviceTemplateGenerationMode mode;
        auto modeIt = generationModes.find(params[0]);
        if (modeIt != generationModes.end()) {
            mode = modeIt->second;
        } else {
            Error.Log() << "Unsupported generation mode: " << params[0];
            PrintDeviceTemplateGenerationOptionsUsage(appName);
            return;
        }

        TSerialPortSettings portSettings;
        auto portParams = WBMQTT::StringSplit(params[2], "-");
        portSettings.Device = params[1];
        portSettings.BaudRate = atoi(portParams[0].c_str());
        portSettings.DataBits = atoi(portParams[1].c_str());
        portSettings.Parity = portParams[2][0];
        portSettings.StopBits = atoi(portParams[3].c_str());
        auto port = std::make_shared<TSerialPort>(portSettings);

        auto protocolParams = WBMQTT::StringSplit(params[3], ":");
        auto slaveId = (protocolParams.size() == 1) ? std::string() : protocolParams[1];

        params.erase(params.begin(), params.begin() + REQUIRED_PARAMS_COUNT);
        if (protocolParams[0] == "dlms_hdlc") {
            DLMS::GenerateDeviceTemplate(static_cast<TDeviceTemplateGenerationMode>(mode),
                                         port,
                                         slaveId,
                                         destinationDir,
                                         params);
            return;
        }
        Error.Log() << "Unknown protocol: " + protocolParams[0];
        PrintDeviceTemplateGenerationOptionsUsage(appName);
    } catch (const exception& e) {
        Error.Log() << e.what();
        exit(EXIT_FAILURE);
    }
}
