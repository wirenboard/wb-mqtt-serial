#include "rpc/rpc_device_load_config_task.h"
#include <wblib/testing/testlog.h>

using namespace WBMQTT;
using namespace WBMQTT::Testing;

TEST(TDeviceLoadConfigTest, CreateRegisterList)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    auto commonDeviceSchema(
        WBMQTT::JSON::Parse(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-confed-common.schema.json")));
    Json::Value templatesSchema(
        LoadConfigTemplatesSchema(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"),
                                  commonDeviceSchema));
    TTemplateMap templates(templatesSchema);
    templates.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    std::vector<std::string> typeList = {"parameters_array", "parameters_object"};
    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    for (size_t i = 0; i < typeList.size(); ++i) {
        const std::string& type = typeList[i];
        auto deviceTemplate = templates.GetTemplate(type)->GetTemplate();
        TRPCRegisterList registerList =
            CreateRegisterList(protocolParams, nullptr, deviceTemplate["parameters"], "1.2.3");
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

TEST(TDeviceLoadConfigTest, CheckParametersConditions)
{
    auto commonDeviceSchema(
        WBMQTT::JSON::Parse(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-confed-common.schema.json")));
    Json::Value templatesSchema(
        LoadConfigTemplatesSchema(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"),
                                  commonDeviceSchema));
    TTemplateMap templates(templatesSchema);
    templates.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    std::vector<std::string> typeList = {"parameters_array", "parameters_object"};
    for (size_t i = 0; i < typeList.size(); ++i) {
        const std::string& type = typeList[i];
        auto deviceTemplate = templates.GetTemplate(type)->GetTemplate();
        auto json(
            JSON::Parse(TLoggedFixture::GetDataFilePath("device_load_config_test/" + type + "_read_values.json")));
        CheckParametersConditions(deviceTemplate["parameters"], json);
        auto match(
            JSON::Parse(TLoggedFixture::GetDataFilePath("device_load_config_test/" + type + "_match_values.json")));
        ASSERT_TRUE(JsonsMatch(json, match)) << type;
    }
}
