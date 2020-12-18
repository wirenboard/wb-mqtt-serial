#include "serial_config.h"

class TSubDevicesTemplateMap: public ITemplateMap
{
        std::unordered_map<std::string, Json::Value> Templates;
    public:
        TSubDevicesTemplateMap(const Json::Value& device);

        const Json::Value& GetTemplate(const std::string& deviceType) override;

        std::vector<std::string> GetDeviceTypes() const override;
};

Json::Value MakeJsonForConfed(const std::string& configFileName, const Json::Value& configSchema, TTemplateMap& templates);
