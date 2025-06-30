#include "confed_json_generator.h"
#include "subdevices_config/confed_json_generator.h"

using namespace WBMQTT::JSON;

Json::Value MakeJsonForConfed(const std::string& configFileName, TTemplateMap& templates)
{
    Json::Value root(Parse(configFileName));
    // If a file contains some symbols, but not a valid JSON,
    // jsoncpp can return Json::nullValue instead of throwing an error
    if (!root.isObject()) {
        throw std::runtime_error("Failed to parse " + configFileName + ". The file is not a valid JSON");
    }
    FixOldConfigFormat(root, templates);
    if (root.isMember("ports")) {
        for (Json::Value& port: root["ports"]) {
            if (port.isMember("devices")) {
                for (Json::Value& device: port["devices"]) {
                    if (device.isMember("device_type")) {
                        PDeviceTemplate deviceTemplate;
                        try {
                            deviceTemplate = templates.GetTemplate(device["device_type"].asString());
                        } catch (...) {
                            // Pass unknown device types as is, homeui will handle them
                            continue;
                        }

                        // Configs with deprecated device templates must be preprocessed for homeui
                        if (deviceTemplate->WithSubdevices()) {
                            device = Subdevices::MakeDeviceJsonForConfed(device, *deviceTemplate);
                        }

                        // Pass configs with modern device templates as is
                    }
                    // Pass configs without device_type as is
                }
            }
        }
    }

    return root;
}