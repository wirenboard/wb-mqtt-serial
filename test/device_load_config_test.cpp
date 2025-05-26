#include "rpc/rpc_device_load_config_task.h"
#include "test_utils.h"
#include <wblib/testing/testlog.h>

using namespace WBMQTT;
using namespace WBMQTT::Testing;

/**
 * Checks that the register lists contains only parameters compatible with specific firmware version.
 * Uses JSON-objects containing parameter ids with register addresses for result matching.
 */
TEST(TDeviceLoadConfigTest, CreateRegisterList)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    std::vector<std::string> typeList = {"parameters_array", "parameters_object"};
    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    for (size_t i = 0; i < typeList.size(); ++i) {
        const std::string& type = typeList[i];
        auto deviceTemplate = templateMap.GetTemplate(type)->GetTemplate();
        TRPCRegisterList registerList = CreateRegisterList(protocolParams,
                                                           nullptr,
                                                           deviceTemplate["parameters"],
                                                           Json::Value(),
                                                           std::string(),
                                                           "1.2.3");
        Json::Value json;
        for (size_t i = 0; i < registerList.size(); ++i) {
            auto& reg = registerList[i];
            json[reg.first] = static_cast<int>(GetUint32RegisterAddress(reg.second->GetConfig()->GetAddress()));
        }
        auto match(
            JSON::Parse(TLoggedFixture::GetDataFilePath("device_load_config_test/" + type + "_register_list.json")));
        ASSERT_TRUE(JsonsMatch(json, match)) << type;
    }
}

/**
 * Checks that the parameter list is not contains items unmatched with template conditions.
 * Uses JSON-objects containig fake read values and result data for matching.
 */
TEST(TDeviceLoadConfigTest, CheckParametersConditions)
{
    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    std::vector<std::string> typeList = {"parameters_array", "parameters_object"};
    for (size_t i = 0; i < typeList.size(); ++i) {
        const std::string& type = typeList[i];
        auto deviceTemplate = templateMap.GetTemplate(type)->GetTemplate();
        auto json(
            JSON::Parse(TLoggedFixture::GetDataFilePath("device_load_config_test/" + type + "_read_values.json")));
        CheckParametersConditions(deviceTemplate["parameters"], json);
        auto match(
            JSON::Parse(TLoggedFixture::GetDataFilePath("device_load_config_test/" + type + "_match_values.json")));
        ASSERT_TRUE(JsonsMatch(json, match)) << type;
    }
}
