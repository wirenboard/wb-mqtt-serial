#include "confed_config_generator.h"
#include "subdevices_config/confed_config_generator.h"

Json::Value MakeConfigFromConfed(std::istream& stream, TTemplateMap& templates)
{
    Json::Value root;
    Json::CharReaderBuilder readerBuilder;
    Json::String errs;

    if (!Json::parseFromStream(readerBuilder, stream, &root, &errs)) {
        throw std::runtime_error("Failed to parse JSON:" + errs);
    }

    for (Json::Value& port: root["ports"]) {
        for (Json::Value& device: port["devices"]) {
            if (device.isMember("device_type")) {
                auto dt = device["device_type"].asString();
                if (dt == "unknown") {
                    auto v = device["value"];
                    device.removeMember("device_type");
                    device.removeMember("value");
                    device = v;
                } else {
                    auto deviceTemplate = templates.GetTemplate(dt);

                    // JSON from homeui for deprecated device templates must be preprocessed
                    if (deviceTemplate->WithSubdevices()) {
                        Subdevices::MakeDeviceConfigFromConfed(device, dt, deviceTemplate->GetTemplate());
                    }

                    // Use JSON from homeui with modern device templates as is
                }
            }
        }
    }

    return root;
}
