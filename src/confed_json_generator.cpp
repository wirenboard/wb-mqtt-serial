#include "confed_json_generator.h"
#include "old_serial_config.h"

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
    return root;
}
