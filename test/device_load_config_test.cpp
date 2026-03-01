#include "rpc/rpc_device_load_config_task.h"
#include "rpc/rpc_device_load_task.h"
#include "rpc/rpc_exception.h"
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
 * Checks that readonly parameters are excluded from the register list
 * when the filtering from rpc_device_load_config_task is applied.
 * This mirrors the readonly filtering in ExecRPCLoadConfigRequest().
 */
TEST(TDeviceLoadConfigTest, ReadonlyParametersFiltered)
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
        auto templateParams = deviceTemplate["parameters"];

        // Filter out readonly parameters, same as rpc_device_load_config_task.cpp
        Json::Value writableParams(templateParams.isObject() ? Json::objectValue : Json::arrayValue);
        if (templateParams.isObject()) {
            for (auto it = templateParams.begin(); it != templateParams.end(); ++it) {
                if (!(*it)["readonly"].asBool()) {
                    writableParams[it.key().asString()] = *it;
                }
            }
        } else {
            for (const auto& item: templateParams) {
                if (!item["readonly"].asBool()) {
                    writableParams.append(item);
                }
            }
        }

        TRPCRegisterList registerList =
            CreateRegisterList(protocolParams, nullptr, writableParams, Json::Value(), "1.2.3");

        // Verify no readonly parameters in the result
        for (const auto& reg: registerList) {
            // p6 in parameters_array and p4 in parameters_object are readonly
            if (type == "parameters_array") {
                ASSERT_NE(reg.Id, "p6") << "readonly parameter p6 should be filtered out";
            } else {
                ASSERT_NE(reg.Id, "p4") << "readonly parameter p4 should be filtered out";
            }
        }

        // Verify the list matches expected non-readonly registers
        Json::Value json;
        for (const auto& reg: registerList) {
            json[reg.Id] = static_cast<int>(GetUint32RegisterAddress(reg.Register->GetConfig()->GetAddress()));
        }
        auto match(JSON::Parse(
            TLoggedFixture::GetDataFilePath("device_load_config_test/" + type + "_writable_register_list.json")));
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

/**
 * Checks that GetConditionParametersRegisterList returns only parameters
 * referenced in channel conditions (mode, variant), not unrelated ones.
 */
TEST(TDeviceLoadTest, GetConditionParametersRegisterList)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templateMap.GetTemplate("device_load_conditions");

    TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);

    auto registerList = request.GetConditionParametersRegisterList();

    std::set<std::string> ids;
    for (const auto& reg: registerList) {
        ids.insert(reg.Id);
    }

    // "mode" and "variant" are referenced in channel conditions
    ASSERT_TRUE(ids.count("mode")) << "mode should be in condition parameters";
    ASSERT_TRUE(ids.count("variant")) << "variant should be in condition parameters";
    // "unrelated" is not referenced in any condition
    ASSERT_FALSE(ids.count("unrelated")) << "unrelated should not be in condition parameters";

    ASSERT_EQ(registerList.size(), 2u);
}

/**
 * Checks that GetChannelsRegisterList without condition params returns all readable channels.
 */
TEST(TDeviceLoadTest, GetChannelsRegisterListAllChannels)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templateMap.GetTemplate("device_load_conditions");

    TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
    // Channels list is empty — should return all readable channels

    auto registerList = request.GetChannelsRegisterList();

    std::set<std::string> ids;
    for (const auto& reg: registerList) {
        ids.insert(reg.Id);
    }

    // 3 readable channels (Write Only has no address, only write_address)
    ASSERT_EQ(registerList.size(), 3u);
    ASSERT_TRUE(ids.count("Always Visible"));
    ASSERT_TRUE(ids.count("Mode Dependent"));
    ASSERT_TRUE(ids.count("Variant Dependent"));
}

/**
 * Checks that GetChannelsRegisterList with condition params filters channels correctly.
 */
TEST(TDeviceLoadTest, GetChannelsRegisterListConditionFiltering)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");

    // Case 1: mode=0, variant=0 — only "Always Visible" should pass
    {
        auto deviceTemplate = templateMap.GetTemplate("device_load_conditions");
        TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);

        Json::Value condParams;
        condParams["mode"] = 0;
        condParams["variant"] = 0;

        auto registerList = request.GetChannelsRegisterList(condParams);
        std::set<std::string> ids;
        for (const auto& reg: registerList) {
            ids.insert(reg.Id);
        }

        ASSERT_EQ(registerList.size(), 1u) << "mode=0, variant=0: only unconditional channel";
        ASSERT_TRUE(ids.count("Always Visible"));
    }

    // Case 2: mode=1, variant=0 — "Always Visible" + "Mode Dependent"
    {
        auto deviceTemplate = templateMap.GetTemplate("device_load_conditions");
        TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);

        Json::Value condParams;
        condParams["mode"] = 1;
        condParams["variant"] = 0;

        auto registerList = request.GetChannelsRegisterList(condParams);
        std::set<std::string> ids;
        for (const auto& reg: registerList) {
            ids.insert(reg.Id);
        }

        ASSERT_EQ(registerList.size(), 2u) << "mode=1, variant=0: unconditional + mode dependent";
        ASSERT_TRUE(ids.count("Always Visible"));
        ASSERT_TRUE(ids.count("Mode Dependent"));
    }

    // Case 3: mode=0, variant=2 — "Always Visible" + "Variant Dependent"
    {
        auto deviceTemplate = templateMap.GetTemplate("device_load_conditions");
        TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);

        Json::Value condParams;
        condParams["mode"] = 0;
        condParams["variant"] = 2;

        auto registerList = request.GetChannelsRegisterList(condParams);
        std::set<std::string> ids;
        for (const auto& reg: registerList) {
            ids.insert(reg.Id);
        }

        ASSERT_EQ(registerList.size(), 2u) << "mode=0, variant=2: unconditional + variant dependent";
        ASSERT_TRUE(ids.count("Always Visible"));
        ASSERT_TRUE(ids.count("Variant Dependent"));
    }

    // Case 4: mode=1, variant=2 — all 3 readable channels
    {
        auto deviceTemplate = templateMap.GetTemplate("device_load_conditions");
        TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);

        Json::Value condParams;
        condParams["mode"] = 1;
        condParams["variant"] = 2;

        auto registerList = request.GetChannelsRegisterList(condParams);
        std::set<std::string> ids;
        for (const auto& reg: registerList) {
            ids.insert(reg.Id);
        }

        ASSERT_EQ(registerList.size(), 3u) << "mode=1, variant=2: all conditions met";
        ASSERT_TRUE(ids.count("Always Visible"));
        ASSERT_TRUE(ids.count("Mode Dependent"));
        ASSERT_TRUE(ids.count("Variant Dependent"));
    }
}

/**
 * Checks that GetParametersRegisterList throws TRPCException
 * when a requested parameter name does not exist in the template.
 */
TEST(TDeviceLoadTest, GetParametersRegisterListThrowsOnUnknownParam)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templateMap.GetTemplate("device_load_conditions");

    TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
    request.Parameters.push_back("nonexistent_param");

    ASSERT_THROW(request.GetParametersRegisterList(), TRPCException)
        << "should throw when requested parameter does not exist in template";
}

/**
 * Checks that ParseRequestItems deduplicates and preserves order.
 */
TEST(TDeviceLoadTest, ParseRequestItems)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templateMap.GetTemplate("device_load_conditions");

    TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);

    Json::Value items(Json::arrayValue);
    items.append("alpha");
    items.append("beta");
    items.append("alpha");
    items.append("gamma");

    std::list<std::string> result;
    request.ParseRequestItems(items, result);

    ASSERT_EQ(result.size(), 3u);
    auto it = result.begin();
    ASSERT_EQ(*it++, "alpha");
    ASSERT_EQ(*it++, "beta");
    ASSERT_EQ(*it++, "gamma");
}

/**
 * Checks that ParseRequestItems handles empty array.
 */
TEST(TDeviceLoadTest, ParseRequestItemsEmpty)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templateMap.GetTemplate("device_load_conditions");

    TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);

    Json::Value items(Json::arrayValue);
    std::list<std::string> result;
    request.ParseRequestItems(items, result);

    ASSERT_TRUE(result.empty());
}

/**
 * Checks that GetChannelsRegisterList returns only specific requested channels.
 */
TEST(TDeviceLoadTest, GetChannelsRegisterListSpecificChannels)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templateMap.GetTemplate("device_load_conditions");

    TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
    request.Channels.push_back("Always Visible");
    request.Channels.push_back("Mode Dependent");

    auto registerList = request.GetChannelsRegisterList();

    ASSERT_EQ(registerList.size(), 2u);
    std::set<std::string> ids;
    for (const auto& reg: registerList) {
        ids.insert(reg.Id);
    }
    ASSERT_TRUE(ids.count("Always Visible"));
    ASSERT_TRUE(ids.count("Mode Dependent"));
}

/**
 * Checks that GetChannelsRegisterList throws when requesting an unknown channel.
 */
TEST(TDeviceLoadTest, GetChannelsRegisterListUnknownChannel)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templateMap.GetTemplate("device_load_conditions");

    TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
    request.Channels.push_back("Nonexistent");

    ASSERT_THROW(request.GetChannelsRegisterList(), TRPCException);
}

/**
 * Checks that GetChannelsRegisterList throws when requesting a write-only channel.
 */
TEST(TDeviceLoadTest, GetChannelsRegisterListWriteOnlyChannel)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templateMap.GetTemplate("device_load_conditions");

    TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
    request.Channels.push_back("Write Only");

    ASSERT_THROW(request.GetChannelsRegisterList(), TRPCException);
}

/**
 * Checks GetParametersRegisterList returns registers for requested params (array format).
 */
TEST(TDeviceLoadTest, GetParametersRegisterListHappyPath)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templateMap.GetTemplate("device_load_conditions");

    TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
    request.Parameters.push_back("mode");
    request.Parameters.push_back("unrelated");

    auto registerList = request.GetParametersRegisterList();

    ASSERT_EQ(registerList.size(), 2u);
    std::set<std::string> ids;
    for (const auto& reg: registerList) {
        ids.insert(reg.Id);
    }
    ASSERT_TRUE(ids.count("mode"));
    ASSERT_TRUE(ids.count("unrelated"));
}

/**
 * Checks GetParametersRegisterList works with object-format parameters.
 */
TEST(TDeviceLoadTest, GetParametersRegisterListObjectFormat)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templateMap.GetTemplate("parameters_object");

    TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
    request.Parameters.push_back("p1");
    request.Parameters.push_back("p5");

    auto registerList = request.GetParametersRegisterList();

    ASSERT_EQ(registerList.size(), 2u);
    std::set<std::string> ids;
    for (const auto& reg: registerList) {
        ids.insert(reg.Id);
    }
    ASSERT_TRUE(ids.count("p1"));
    ASSERT_TRUE(ids.count("p5"));
}

/**
 * Checks ParseRPCDeviceLoadRequest factory: deduplicates channels and parameters,
 * populates OnResult/OnError callbacks.
 */
TEST(TDeviceLoadTest, ParseRPCDeviceLoadRequest)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templateMap.GetTemplate("device_load_conditions");

    Json::Value requestJson(Json::objectValue);
    Json::Value channels(Json::arrayValue);
    channels.append("Always Visible");
    channels.append("Mode Dependent");
    channels.append("Always Visible"); // duplicate
    requestJson["channels"] = channels;

    Json::Value parameters(Json::arrayValue);
    parameters.append("mode");
    parameters.append("unrelated");
    parameters.append("mode"); // duplicate
    requestJson["parameters"] = parameters;

    bool resultCalled = false;
    auto onResult = [&resultCalled](const Json::Value&) { resultCalled = true; };
    auto onError = [](int, const std::string&) {};

    auto parsed =
        ParseRPCDeviceLoadRequest(requestJson, protocolParams, nullptr, deviceTemplate, false, onResult, onError);

    // Channels should be deduplicated
    ASSERT_EQ(parsed->Channels.size(), 2u);
    auto chIt = parsed->Channels.begin();
    ASSERT_EQ(*chIt++, "Always Visible");
    ASSERT_EQ(*chIt++, "Mode Dependent");

    // Parameters should be deduplicated
    ASSERT_EQ(parsed->Parameters.size(), 2u);
    auto pIt = parsed->Parameters.begin();
    ASSERT_EQ(*pIt++, "mode");
    ASSERT_EQ(*pIt++, "unrelated");

    // Callbacks should be set
    ASSERT_TRUE(parsed->OnResult != nullptr);
    ASSERT_TRUE(parsed->OnError != nullptr);
}

/**
 * Checks ParseRPCDeviceLoadConfigRequest factory: force flag, callbacks bound.
 */
TEST(TDeviceLoadConfigTest, ParseRPCDeviceLoadConfigRequest)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templateMap.GetTemplate("device_load_conditions");

    Json::Value requestJson(Json::objectValue);
    requestJson["force"] = true;

    bool resultCalled = false;
    auto onResult = [&resultCalled](const Json::Value&) { resultCalled = true; };
    auto onError = [](int, const std::string&) {};

    std::string configFileName = "/tmp/test-config.json";
    TRPCDeviceParametersCache parametersCache;

    auto parsed = ParseRPCDeviceLoadConfigRequest(requestJson,
                                                  protocolParams,
                                                  nullptr,
                                                  deviceTemplate,
                                                  true,
                                                  configFileName,
                                                  parametersCache,
                                                  onResult,
                                                  onError);

    ASSERT_TRUE(parsed->Force);
    ASSERT_TRUE(parsed->DeviceFromConfig);
    ASSERT_EQ(&parsed->ConfigFileName, &configFileName);
    ASSERT_EQ(&parsed->ParametersCache, &parametersCache);
    ASSERT_TRUE(parsed->OnResult != nullptr);
    ASSERT_TRUE(parsed->OnError != nullptr);
}
