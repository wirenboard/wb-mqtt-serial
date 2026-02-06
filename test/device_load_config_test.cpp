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
        TRPCRegisterList registerList =
            CreateRegisterList(protocolParams, nullptr, deviceTemplate["parameters"], Json::Value(), "1.2.3");
        Json::Value json;
        for (size_t i = 0; i < registerList.size(); ++i) {
            auto& reg = registerList[i];
            json[reg.Id] = static_cast<int>(GetUint32RegisterAddress(reg.Register->GetConfig()->GetAddress()));
        }
        auto match(
            JSON::Parse(TLoggedFixture::GetDataFilePath("device_load_config_test/" + type + "_register_list.json")));
        ASSERT_TRUE(JsonsMatch(json, match)) << type;
    }
}

/**
 * Checks that the register lists contains only parameters of specified group and their condition parameter
 * (recoursive). Uses JSON-objects containing parameter ids with register addresses for result matching.
 */
TEST(TDeviceLoadConfigTest, CreateGroupRegisterList)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    std::vector<std::string> typeList = {"parameters_group_array", "parameters_group_object"};
    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    for (size_t i = 0; i < typeList.size(); ++i) {
        const std::string& type = typeList[i];
        auto deviceTemplate = templateMap.GetTemplate(type)->GetTemplate();
        std::list<std::string> paramsList;
        TRPCRegisterList registerList =
            CreateRegisterList(protocolParams,
                               nullptr,
                               GetTemplateParamsGroup(deviceTemplate["parameters"], "g2", paramsList),
                               Json::Value(),
                               std::string());
        Json::Value json;
        for (size_t i = 0; i < registerList.size(); ++i) {
            auto& reg = registerList[i];
            json[reg.Id] = static_cast<int>(GetUint32RegisterAddress(reg.Register->GetConfig()->GetAddress()));
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

/**
 * Checks that the register values passed to JSON without loss of precision.
 */
TEST(TDeviceLoadConfigTest, RawValueToJson)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    auto deviceTemplate = templateMap.GetTemplate("parameters_to_json")->GetTemplate();
    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    TRPCRegisterList registerList =
        CreateRegisterList(protocolParams, nullptr, deviceTemplate["parameters"], Json::Value());

    int index = 0;
    std::string stringValue;
    for (const auto& item: registerList) {
        switch (index++) {
            case 0:
                stringValue = "214.72"; // s16 with 0.01 scale
                break;
            case 1:
                stringValue = "-459234512454223413"; // s64
                break;
            case 2:
                stringValue = "257080185625143217"; // u64
                break;
            case 3:
                stringValue = "524673325613.12"; // double
                break;
            case 4:
                stringValue = "test"; // string
                break;
        }
        item.Register->SetValue(ConvertToRawValue(*item.Register->GetConfig(), stringValue));
        ASSERT_EQ(RawValueToJSON(*item.Register->GetConfig(), item.Register->GetValue()).asString(), stringValue);
    }
}
