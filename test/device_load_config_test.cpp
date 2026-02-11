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
            CreateRegisterList(protocolParams, nullptr, deviceTemplate["parameters"], nullptr, "1.2.3");
        Json::Value json;
        for (const auto& reg: registerList) {
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
TEST(TDeviceLoadConfigTest, GetRegisterListParameters)
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
        TRPCRegisterList registerList = CreateRegisterList(protocolParams, nullptr, deviceTemplate["parameters"]);
        auto data(
            JSON::Parse(TLoggedFixture::GetDataFilePath("device_load_config_test/" + type + "_read_values.json")));
        for (const auto& reg: registerList) {
            if (data.isMember(reg.Id)) {
                reg.Register->SetValue(TRegisterValue(data[reg.Id].asInt()));
            }
        }

        Json::Value json;
        GetRegisterListParameters(registerList, json);

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

    TRPCRegisterList registerList = CreateRegisterList(protocolParams, nullptr, deviceTemplate["parameters"]);
    ASSERT_EQ(registerList.size(), 5);

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

/**
 * Checks that "no cache" parameters are removed from known parameters list and added to register list
 */
TEST(TDeviceLoadConfigTest, NoCache)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    auto deviceTemplate = templateMap.GetTemplate("parameters_no_cache")->GetTemplate();
    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");

    auto json(JSON::Parse(TLoggedFixture::GetDataFilePath("device_load_config_test/parameters_no_cache_values.json")));
    TRPCRegisterList registerList = CreateRegisterList(protocolParams, nullptr, deviceTemplate["parameters"], &json);
    ASSERT_EQ(registerList.size(), 4);

    int index = 0;
    for (const auto& item: registerList) {
        switch (index++) {
            case 0:
                ASSERT_EQ(item.Id, "p3");
                item.Register->SetValue(TRegisterValue(12));
                break;
            case 1:
                ASSERT_EQ(item.Id, "p4");
                item.Register->SetValue(TRegisterValue(34));
                break;
            case 2:
                ASSERT_EQ(item.Id, "p5");
                item.Register->SetValue(TRegisterValue(56));
                break;
            case 3:
                ASSERT_EQ(item.Id, "p6");
                item.Register->SetValue(TRegisterValue(78));
                break;
        }
    }

    GetRegisterListParameters(registerList, json);

    auto match(JSON::Parse(TLoggedFixture::GetDataFilePath("device_load_config_test/parameters_no_cache_match.json")));
    ASSERT_TRUE(JsonsMatch(json, match));
}