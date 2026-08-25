#include "fake_serial_port.h"
#include "port/feature_port.h"
#include "rpc/rpc_device_load_config_task.h"
#include "rpc/rpc_device_load_task.h"
#include "rpc/rpc_exception.h"
#include "test_utils.h"
#include <wblib/testing/testlog.h>

using namespace WBMQTT;
using namespace WBMQTT::Testing;

namespace
{
    //! The register list functions take the firmware version and the "is Wiren Board device" flag from the device
    PSerialDevice MakeDevice(const TDeviceProtocolParams& protocolParams,
                             const Json::Value& deviceTemplate,
                             const std::string& fwVersion,
                             bool wbDevice)
    {
        auto config = std::make_shared<TDeviceConfig>("test", "1", "modbus");
        auto device = protocolParams.factory->CreateDevice(deviceTemplate, config, protocolParams.protocol);
        device->SetWbFwVersion(fwVersion);
        device->SetWbDevice(wbDevice);
        return device;
    }
}

/**
 * Checks that the register lists contains only parameters compatible with specific firmware version.
 * Uses JSON-objects containing parameter ids with register addresses for result matching.
 */
TEST(TDeviceLoadConfigTest, CreateParametersRegisterList)
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
        auto device = MakeDevice(protocolParams, deviceTemplate, "1.2.3", false);
        TRPCRegisterList registerList =
            CreateParametersRegisterList(protocolParams, device, deviceTemplate["parameters"]);
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
 * Checks register list creation for parameter fw variants (declarations with the same id and different fw).
 * One register per parameter and condition is created, a parameter is read if any of its declarations
 * is supported by the device firmware version. The order of declarations does not affect the result.
 */
TEST(TDeviceLoadConfigTest, CreateRegisterListFwVariants)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    auto deviceTemplate = templateMap.GetTemplate("parameters_fw_variants")->GetTemplate();
    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");

    auto makeIdsJson = [&](const Json::Value& parameters, const std::string& fwVersion) {
        auto device = MakeDevice(protocolParams, deviceTemplate, fwVersion, false);
        TRPCRegisterList registerList = CreateParametersRegisterList(protocolParams, device, parameters);
        Json::Value json;
        for (const auto& reg: registerList) {
            json[reg.Id] = true;
        }
        // a group of declarations with the same id and condition must produce a single register,
        // duplicates would be hidden by the id-keyed json
        EXPECT_EQ(registerList.size(), json.size());
        return json;
    };

    // The order of declarations in the template must not affect the result
    Json::Value reversedParameters(Json::arrayValue);
    for (Json::ArrayIndex i = deviceTemplate["parameters"].size(); i > 0; --i) {
        reversedParameters.append(deviceTemplate["parameters"][i - 1]);
    }

    for (const auto& parameters: {deviceTemplate["parameters"], reversedParameters}) {
        // Old firmware: the parameter added in a newer firmware is filtered out
        Json::Value oldFw;
        oldFw["mode"] = true;
        oldFw["p1"] = true;
        oldFw["p3"] = true;
        ASSERT_TRUE(JsonsMatch(makeIdsJson(parameters, "2.0.0"), oldFw));

        // New firmware: every parameter is read once
        Json::Value newFw(oldFw);
        newFw["p2"] = true;
        ASSERT_TRUE(JsonsMatch(makeIdsJson(parameters, "2.4.0"), newFw));

        // Unknown firmware: every parameter is read once
        ASSERT_TRUE(JsonsMatch(makeIdsJson(parameters, std::string()), newFw));
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
        TRPCRegisterList registerList =
            CreateParametersRegisterList(protocolParams, nullptr, deviceTemplate["parameters"]);
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
    TRPCRegisterList registerList = CreateParametersRegisterList(protocolParams, nullptr, deviceTemplate["parameters"]);

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
 * Checks that only the registers that got a value are merged: an unread register, a register
 * with a read error and a register the device does not support give no value. The members
 * of the object outside the register list are kept.
 */
TEST(TDeviceLoadTest, MergeRegisterListValues)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templateMap.GetTemplate("device_load_conditions")->GetTemplate();

    TRPCRegisterList registerList = CreateParametersRegisterList(protocolParams, nullptr, deviceTemplate["parameters"]);
    for (auto& reg: registerList) {
        if (reg.Id == "mode") {
            reg.Register->SetValue(TRegisterValue(1));
        }
        if (reg.Id == "variant") {
            // the device has no such register, its value must not be taken
            reg.Register->SetValue(TRegisterValue(7));
            reg.Register->SetSupported(false);
        }
        // "unrelated" keeps the undefined value of an unread register
    }

    Json::Value values;
    values["mode"] = 0;
    values["from_request"] = 42;
    MergeRegisterListValues(registerList, values);

    ASSERT_EQ(values.size(), 2u) << "only the read register value should be merged";
    EXPECT_EQ(values["mode"].asInt(), 1) << "the value of a read register should be replaced";
    EXPECT_EQ(values["from_request"].asInt(), 42) << "a member outside the register list should be kept";

    // the read error is set by a failed read and by the "error_value" and "unsupported_value" markers
    for (auto& reg: registerList) {
        if (reg.Id == "mode") {
            reg.Register->SetValue(TRegisterValue(2));
            reg.Register->SetError(TRegister::TError::ReadError);
        }
    }
    MergeRegisterListValues(registerList, values);
    EXPECT_EQ(values["mode"].asInt(), 1) << "the value of a register with a read error should not be merged";
}

class TRPCDeviceLoadTaskExecTest: public TLoggedFixture
{};

/**
 * Checks that a device/Load request does not fail when a channel condition parameter
 * could not be read: the conditions over it are evaluated with an undefined value.
 * The "mode==1" item is not included in the answer like an item with a false condition,
 * the "mode!=1" item is read and comes in the answer, the rest is read as usual.
 */
TEST_F(TRPCDeviceLoadTaskExecTest, DropsItemsOnUnreadConditionParameter)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);
    TTemplateMap templates(GetTemplatesSchema());
    templates.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templates.GetTemplate("device_load_conditions");
    auto config = std::make_shared<TDeviceConfig>("test", "1", "modbus");
    auto device = protocolParams.factory->CreateDevice(deviceTemplate->GetTemplate(), config, protocolParams.protocol);

    auto request = std::make_shared<TRPCDeviceLoadRequest>(protocolParams, device, deviceTemplate, true);
    request->Channels = {"Always Visible", "Mode Dependent", "Variant Dependent", "Mode Not One"};
    Json::Value result;
    bool gotResult = false;
    std::string error;
    request->OnResult = [&](const Json::Value& data) {
        result = data;
        gotResult = true;
    };
    request->OnError = [&](auto, const std::string& message) { error = message; };

    auto port = std::make_shared<TFakeSerialPort>(*this, "<device_load_conditions>");
    // the device confirms continuous read support on session preparation,
    // so a failed read does not mark the registers unsupported
    port->Expect({0x01, 0x03, 0x00, 0x72, 0x00, 0x01, 0x24, 0x11},
                 {0x01, 0x03, 0x02, 0x00, 0x00, 0xB8, 0x44},
                 "read continuous read state");
    port->Expect({0x01, 0x06, 0x00, 0x72, 0x00, 0x01, 0xE8, 0x11},
                 {0x01, 0x06, 0x00, 0x72, 0x00, 0x01, 0xE8, 0x11},
                 "enable continuous read");
    // the read of the condition parameter "mode" fails, the register stays supported with no value
    port->Expect({0x01, 0x03, 0x00, 0x64, 0x00, 0x01, 0xC5, 0xD5}, {0x01, 0x83, 0x02, 0xC0, 0xF1}, "read mode");
    // "variant" is read as 0, so the "variant==2" condition of "Variant Dependent" is false
    port->Expect({0x01, 0x03, 0x00, 0x65, 0x00, 0x01, 0x94, 0x15},
                 {0x01, 0x03, 0x02, 0x00, 0x00, 0xB8, 0x44},
                 "read variant");
    // the unconditional channel is read as usual
    port->Expect({0x01, 0x04, 0x00, 0xC8, 0x00, 0x01, 0xB0, 0x34},
                 {0x01, 0x04, 0x02, 0x00, 0x2A, 0x38, 0xEF},
                 "read Always Visible");
    // the "mode!=1" condition is true with the undefined "mode", so the channel is read
    port->Expect({0x01, 0x04, 0x00, 0xCD, 0x00, 0x01, 0xA0, 0x35},
                 {0x01, 0x04, 0x02, 0x00, 0x07, 0xF8, 0xF2},
                 "read Mode Not One");

    TSerialClientDeviceAccessHandler accessHandler(nullptr);
    TRPCDeviceLoadSerialClientTask task(request);
    task.Run(std::make_shared<TFeaturePort>(port, false), accessHandler, {});

    ASSERT_TRUE(gotResult) << error;
    EXPECT_EQ(result["channels"]["Always Visible"].asInt(), 42);
    EXPECT_EQ(result["channels"]["Mode Not One"].asInt(), 7);
    EXPECT_FALSE(result["channels"].isMember("Mode Dependent"));
    EXPECT_FALSE(result["channels"].isMember("Variant Dependent"));
    ASSERT_EQ(result["readonly"].size(), 2u);
    EXPECT_EQ(result["readonly"][0].asString(), "Always Visible");
    EXPECT_EQ(result["readonly"][1].asString(), "Mode Not One");
}

/**
 * Checks the 0xFFFE marker handling in device/Load condition parameter reads: the Wiren Board
 * device answers 0xFFFE for an unsupported register instead of a modbus exception. The value
 * is missing, the "mode==1" condition is false with the undefined value, so the dependent
 * item is not included in the answer and the request succeeds. Without the check 65534
 * would enter condition evaluation as a number.
 */
TEST_F(TRPCDeviceLoadTaskExecTest, DropsItemsOnUnsupportedValueMarker)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);
    TTemplateMap templates(GetTemplatesSchema());
    templates.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templates.GetTemplate("device_load_conditions");
    auto config = std::make_shared<TDeviceConfig>("test", "1", "modbus");
    auto device = protocolParams.factory->CreateDevice(deviceTemplate->GetTemplate(), config, protocolParams.protocol);
    // the 0xFFFE marker check runs for Wiren Board devices only
    device->SetWbDevice(true);

    auto request = std::make_shared<TRPCDeviceLoadRequest>(protocolParams, device, deviceTemplate, true);
    request->Channels = {"Mode Dependent"};
    Json::Value result;
    bool gotResult = false;
    std::string error;
    request->OnResult = [&](const Json::Value& data) {
        result = data;
        gotResult = true;
    };
    request->OnError = [&](auto, const std::string& message) { error = message; };

    auto port = std::make_shared<TFakeSerialPort>(*this, "<device_load_conditions>");
    // continuous read state is checked before enabling
    port->Expect({0x01, 0x03, 0x00, 0x72, 0x00, 0x01, 0x24, 0x11},
                 {0x01, 0x03, 0x02, 0x00, 0x00, 0xB8, 0x44},
                 "read continuous read state");
    port->Expect({0x01, 0x06, 0x00, 0x72, 0x00, 0x01, 0xE8, 0x11},
                 {0x01, 0x06, 0x00, 0x72, 0x00, 0x01, 0xE8, 0x11},
                 "enable continuous read");
    // a Wiren Board device firmware version is requested on session preparation
    port->Expect({0x01, 0x03, 0x00, 0xFA, 0x00, 0x10, 0x64, 0x37}, {0x01, 0x83, 0x02, 0xC0, 0xF1}, "read fw version");
    // the condition parameter "mode" is read as 0xFFFE, the unsupported register marker
    port->Expect({0x01, 0x03, 0x00, 0x64, 0x00, 0x01, 0xC5, 0xD5},
                 {0x01, 0x03, 0x02, 0xFF, 0xFE, 0x78, 0x34},
                 "read mode");
    // the marker makes the register suspicious, it is re-read without continuous read
    port->Expect({0x01, 0x06, 0x00, 0x72, 0x00, 0x00, 0x29, 0xD1},
                 {0x01, 0x06, 0x00, 0x72, 0x00, 0x00, 0x29, 0xD1},
                 "disable continuous read");
    // the modbus exception confirms the device does not support the register
    port->Expect({0x01, 0x03, 0x00, 0x64, 0x00, 0x01, 0xC5, 0xD5}, {0x01, 0x83, 0x02, 0xC0, 0xF1}, "re-read mode");
    port->Expect({0x01, 0x06, 0x00, 0x72, 0x00, 0x01, 0xE8, 0x11},
                 {0x01, 0x06, 0x00, 0x72, 0x00, 0x01, 0xE8, 0x11},
                 "enable continuous read");

    TSerialClientDeviceAccessHandler accessHandler(nullptr);
    TRPCDeviceLoadSerialClientTask task(request);
    task.Run(std::make_shared<TFeaturePort>(port, false), accessHandler, {});

    ASSERT_TRUE(gotResult) << error;
    EXPECT_FALSE(result.isMember("channels"));
    EXPECT_FALSE(result.isMember("readonly"));
}

class TRPCDeviceLoadConfigTaskExecTest: public TLoggedFixture
{};

/**
 * Checks that device/LoadConfig checks the 0xFFFE marker before resolving conditions:
 * the confirmed marker on "mode" is a missing value for the conditions, so the dependent
 * declarations are not resolved by the number 65534. Before the fix the "mode>0"
 * declaration of "ov" matched and put the value of "ov" into the answer.
 */
TEST_F(TRPCDeviceLoadConfigTaskExecTest, ChecksUnsupportedValueMarkerBeforeConditions)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);
    TTemplateMap templates(GetTemplatesSchema());
    templates.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templates.GetTemplate("parameters_condition_variants");
    auto config = std::make_shared<TDeviceConfig>("test", "1", "modbus");
    // the 0xFFFE marker check runs for Wiren Board devices with continuous read enabled only,
    // the template check runs for Wiren Board devices
    Json::Value deviceJson = deviceTemplate->GetTemplate();
    deviceJson["enable_wb_continuous_read"] = true;
    auto device = protocolParams.factory->CreateDevice(deviceJson, config, protocolParams.protocol);
    device->SetWbDevice(true);

    TRPCDeviceParametersCache parametersCache;
    std::string configFileName;
    auto request = std::make_shared<TRPCDeviceLoadConfigRequest>(protocolParams,
                                                                 device,
                                                                 deviceTemplate,
                                                                 false,
                                                                 configFileName,
                                                                 parametersCache);
    request->Force = true;
    Json::Value result;
    bool gotResult = false;
    std::string error;
    request->OnResult = [&](const Json::Value& data) {
        result = data;
        gotResult = true;
    };
    request->OnError = [&](auto, const std::string& message) { error = message; };

    auto port = std::make_shared<TFakeSerialPort>(*this, "<parameters_condition_variants>");
    // continuous read is checked and enabled on session preparation
    port->Expect({0x01, 0x03, 0x00, 0x72, 0x00, 0x01, 0x24, 0x11},
                 {0x01, 0x03, 0x02, 0x00, 0x00, 0xB8, 0x44},
                 "read continuous read state");
    port->Expect({0x01, 0x06, 0x00, 0x72, 0x00, 0x01, 0xE8, 0x11},
                 {0x01, 0x06, 0x00, 0x72, 0x00, 0x01, 0xE8, 0x11},
                 "enable continuous read");
    // a Wiren Board device firmware version is requested on session preparation
    port->Expect({0x01, 0x03, 0x00, 0xFA, 0x00, 0x10, 0x64, 0x37}, {0x01, 0x83, 0x02, 0xC0, 0xF1}, "read fw version");
    // the template check reads the device model, the device answers "TESTDEV"
    port->Expect({0x01, 0x03, 0x00, 0xC8, 0x00, 0x14, 0xC4, 0x3B},
                 {0x01, 0x03, 0x28, 0x00, 0x54, 0x00, 0x45, 0x00, 0x53, 0x00, 0x54, 0x00, 0x44, 0x00, 0x45,
                  0x00, 0x56, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xB0, 0xE4},
                 "read model");
    // every template parameter is read, "mode" answers 0xFFFE, the unsupported register marker
    port->Expect({0x01, 0x03, 0x00, 0x01, 0x00, 0x01, 0xD5, 0xCA},
                 {0x01, 0x03, 0x02, 0xFF, 0xFE, 0x78, 0x34},
                 "read mode");
    port->Expect({0x01, 0x03, 0x00, 0x02, 0x00, 0x01, 0x25, 0xCA},
                 {0x01, 0x03, 0x02, 0x00, 0x64, 0xB9, 0xAF},
                 "read sp");
    port->Expect({0x01, 0x03, 0x00, 0x03, 0x00, 0x01, 0x74, 0x0A},
                 {0x01, 0x03, 0x02, 0x00, 0x01, 0x79, 0x84},
                 "read ov");
    port->Expect({0x01, 0x03, 0x00, 0x04, 0x00, 0x01, 0xC5, 0xCB},
                 {0x01, 0x03, 0x02, 0x00, 0x00, 0xB8, 0x44},
                 "read ro");
    port->Expect({0x01, 0x03, 0x00, 0x05, 0x00, 0x01, 0x94, 0x0B},
                 {0x01, 0x03, 0x02, 0x00, 0x00, 0xB8, 0x44},
                 "read romode");
    port->Expect({0x01, 0x03, 0x00, 0x06, 0x00, 0x01, 0x64, 0x0B},
                 {0x01, 0x03, 0x02, 0x00, 0x01, 0x79, 0x84},
                 "read mx");
    port->Expect({0x01, 0x03, 0x00, 0x07, 0x00, 0x01, 0x35, 0xCB},
                 {0x01, 0x03, 0x02, 0x00, 0x00, 0xB8, 0x44},
                 "read other");
    // the marker makes the register suspicious, it is re-read without continuous read
    port->Expect({0x01, 0x06, 0x00, 0x72, 0x00, 0x00, 0x29, 0xD1},
                 {0x01, 0x06, 0x00, 0x72, 0x00, 0x00, 0x29, 0xD1},
                 "disable continuous read");
    // the modbus exception confirms the device does not support the register
    port->Expect({0x01, 0x03, 0x00, 0x01, 0x00, 0x01, 0xD5, 0xCA}, {0x01, 0x83, 0x02, 0xC0, 0xF1}, "re-read mode");
    // the continuous read state found on session preparation is restored
    port->Expect({0x01, 0x06, 0x00, 0x72, 0x00, 0x01, 0xE8, 0x11},
                 {0x01, 0x06, 0x00, 0x72, 0x00, 0x01, 0xE8, 0x11},
                 "restore continuous read");

    TSerialClientDeviceAccessHandler accessHandler(nullptr);
    TRPCDeviceLoadConfigSerialClientTask task(request);
    task.Run(std::make_shared<TFeaturePort>(port, false), accessHandler, {});

    ASSERT_TRUE(gotResult) << error;
    EXPECT_EQ(result["model"].asString(), "TESTDEV");
    EXPECT_EQ(result["parameters"]["mode"].asString(), "unsupported");
    // before the fix "ov" was here, resolved by the "mode>0" condition over 65534
    EXPECT_FALSE(result["parameters"].isMember("ov"));
    EXPECT_FALSE(result["parameters"].isMember("sp"));
    EXPECT_FALSE(result["parameters"].isMember("mx"));
    EXPECT_FALSE(result["parameters"].isMember("ro"));
    EXPECT_EQ(result["parameters"]["romode"].asInt(), 0);
    EXPECT_EQ(result["parameters"]["other"].asInt(), 0);
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

    // 5 readable channels (Write Only has no address, only write_address)
    ASSERT_EQ(registerList.size(), 5u);
    ASSERT_TRUE(ids.count("Always Visible"));
    ASSERT_TRUE(ids.count("Mode Dependent"));
    ASSERT_TRUE(ids.count("Variant Dependent"));
    ASSERT_TRUE(ids.count("Mode Not One"));
    // channels disabled by default in config are still read
    ASSERT_TRUE(ids.count("Disabled Channel"));
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

    // Case 1: mode=0, variant=0, unconditional channels and "Mode Not One" pass
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

        ASSERT_EQ(registerList.size(), 3u) << "mode=0, variant=0: unconditional channels and mode not one";
        ASSERT_TRUE(ids.count("Always Visible"));
        ASSERT_TRUE(ids.count("Disabled Channel"));
        ASSERT_TRUE(ids.count("Mode Not One"));
    }

    // Case 2: mode=1, variant=0, unconditional channels and "Mode Dependent" pass
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

        ASSERT_EQ(registerList.size(), 3u) << "mode=1, variant=0: unconditional + mode dependent";
        ASSERT_TRUE(ids.count("Always Visible"));
        ASSERT_TRUE(ids.count("Disabled Channel"));
        ASSERT_TRUE(ids.count("Mode Dependent"));
    }

    // Case 3: mode=0, variant=2, unconditional channels, "Variant Dependent" and "Mode Not One" pass
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

        ASSERT_EQ(registerList.size(), 4u) << "mode=0, variant=2: unconditional, variant dependent, mode not one";
        ASSERT_TRUE(ids.count("Always Visible"));
        ASSERT_TRUE(ids.count("Disabled Channel"));
        ASSERT_TRUE(ids.count("Variant Dependent"));
        ASSERT_TRUE(ids.count("Mode Not One"));
    }

    // Case 4: mode=1, variant=2, all channels except "Mode Not One" pass
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

        ASSERT_EQ(registerList.size(), 4u) << "mode=1, variant=2: all conditions met except mode not one";
        ASSERT_TRUE(ids.count("Always Visible"));
        ASSERT_TRUE(ids.count("Disabled Channel"));
        ASSERT_TRUE(ids.count("Mode Dependent"));
        ASSERT_TRUE(ids.count("Variant Dependent"));
    }

    // Case 5: no known values, conditions are evaluated with undefined values,
    // equality conditions are false, the "mode!=1" condition is true
    {
        auto deviceTemplate = templateMap.GetTemplate("device_load_conditions");
        TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);

        auto registerList = request.GetChannelsRegisterList(Json::Value(Json::objectValue));
        std::set<std::string> ids;
        for (const auto& reg: registerList) {
            ids.insert(reg.Id);
        }

        ASSERT_EQ(registerList.size(), 3u) << "no known values: unconditional channels and mode not one";
        ASSERT_TRUE(ids.count("Always Visible"));
        ASSERT_TRUE(ids.count("Disabled Channel"));
        ASSERT_TRUE(ids.count("Mode Not One"));
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
    request.Parameters.insert("nonexistent_param");

    ASSERT_THROW(request.GetParametersRegisterList(), TRPCException)
        << "should throw when requested parameter does not exist in template";
}

/**
 * Checks that unsupported value 0xFFFE is detected in channel enums,
 * which are converted to strings on template load.
 */
TEST(TDeviceLoadTest, CheckUnsupportedInChannelEnum)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templateMap.GetTemplate("channel_enum")->GetTemplate();

    auto device = MakeDevice(protocolParams, deviceTemplate, std::string(), true);
    TRPCRegisterList registerList = CreateChannelsRegisterList(protocolParams, device, deviceTemplate["channels"]);

    std::map<std::string, bool> checkUnsupported;
    for (const auto& reg: registerList) {
        checkUnsupported[reg.Id] = reg.CheckUnsupported;
    }

    ASSERT_EQ(checkUnsupported.size(), 5u);
    ASSERT_FALSE(checkUnsupported["unsupported_in_enum"]);
    ASSERT_TRUE(checkUnsupported["supported_enum"]);
    ASSERT_FALSE(checkUnsupported["signed_enum"]);
    ASSERT_FALSE(checkUnsupported["hex_enum"]);
    ASSERT_FALSE(checkUnsupported["range_with_unsupported"]);
}

/**
 * Checks that GetParametersRegisterList selects the declaration of a requested parameter
 * by its condition and the read value is converted using the scale of the acting declaration.
 */
TEST(TDeviceLoadTest, GetParametersRegisterListSelectsDeclarationByCondition)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templateMap.GetTemplate("parameters_condition_variants");

    Json::Value condParams(Json::objectValue);
    condParams["mode"] = 0;
    {
        TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
        request.Parameters.insert("sp");
        auto registerList = request.GetParametersRegisterList(condParams);
        ASSERT_EQ(registerList.size(), 1u);
        registerList.front().Register->SetValue(TRegisterValue(1000));
        auto& reg = registerList.front();
        EXPECT_EQ(RawValueToJSON(*reg.Register->GetConfig(), reg.Register->GetValue()).asInt(), 1000);
    }

    // the declaration acting in mode 1 has scale 0.1, so the same raw value gives another value
    condParams["mode"] = 1;
    {
        TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
        request.Parameters.insert("sp");
        auto registerList = request.GetParametersRegisterList(condParams);
        ASSERT_EQ(registerList.size(), 1u);
        registerList.front().Register->SetValue(TRegisterValue(1000));
        auto& reg = registerList.front();
        EXPECT_EQ(RawValueToJSON(*reg.Register->GetConfig(), reg.Register->GetValue()).asInt(), 100);
    }
}

/**
 * Checks the outcomes of a requested parameter: absence in data if every condition is false,
 * with the conditions over parameters without a value evaluated with an undefined value,
 * an exception if several declarations match at once, mixed outcomes of the declarations
 * of one parameter, and reuse of the value already read for condition evaluation.
 */
TEST(TDeviceLoadTest, GetParametersRegisterListConditionOutcomes)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templateMap.GetTemplate("parameters_condition_variants");

    // "mode" got no value, the equality conditions of the "sp" declarations are false
    // with the undefined value, the parameter is not in the answer
    {
        TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
        request.Parameters.insert("sp");
        Json::Value data(Json::objectValue);
        auto registerList = request.GetParametersRegisterList(Json::Value(Json::objectValue), &data);
        EXPECT_TRUE(registerList.empty());
        EXPECT_TRUE(data.empty());
    }

    // every condition of the "sp" declarations is false, the parameter is not in the answer
    {
        TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
        request.Parameters.insert("sp");
        Json::Value condParams(Json::objectValue);
        condParams["mode"] = 5;
        Json::Value data(Json::objectValue);
        auto registerList = request.GetParametersRegisterList(condParams, &data);
        EXPECT_TRUE(registerList.empty());
        EXPECT_TRUE(data.empty());
    }

    // several declarations of "ov" match at once, a template error
    {
        TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
        request.Parameters.insert("ov");
        Json::Value condParams(Json::objectValue);
        condParams["mode"] = 1;
        try {
            request.GetParametersRegisterList(condParams);
            ADD_FAILURE() << "Expect TRPCException";
        } catch (const TRPCException& e) {
            EXPECT_EQ(std::string(e.what()),
                      "Parameter \"ov\" is ambiguous: several declarations match the condition parameter values");
        }
    }

    // the "mode==1" declaration of "mx" matches and the "other==1" declaration is false
    // because "other" got no value, the parameter is read by the matched declaration
    {
        TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
        request.Parameters.insert("mx");
        Json::Value condParams(Json::objectValue);
        condParams["mode"] = 1;
        Json::Value data(Json::objectValue);
        auto registerList = request.GetParametersRegisterList(condParams, &data);
        ASSERT_EQ(registerList.size(), 1u);
        EXPECT_EQ(registerList.front().Id, "mx");
        EXPECT_TRUE(data.empty()) << "the readable parameter should not get a value before the read";
    }

    // "mode" was already read for condition evaluation, its value goes to data without a second read
    {
        TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
        request.Parameters.insert("mode");
        request.Parameters.insert("sp");
        Json::Value condParams(Json::objectValue);
        condParams["mode"] = 1;
        Json::Value data(Json::objectValue);
        auto registerList = request.GetParametersRegisterList(condParams, &data);
        ASSERT_EQ(registerList.size(), 1u);
        EXPECT_EQ(registerList.front().Id, "sp");
        EXPECT_EQ(data["mode"].asInt(), 1);
    }
}

/**
 * Checks that the fw variants of a requested parameter are not ambiguous for device/Load:
 * the declarations with the same condition and different "fw" form a chain and give a single register.
 */
TEST(TDeviceLoadTest, GetParametersRegisterListFwVariants)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templateMap.GetTemplate("parameters_fw_variants");

    // "p1" has two variants without a condition, both match on a new and on an old firmware
    for (const auto& fwVersion: {"2.4.0", "2.0.0"}) {
        auto device = MakeDevice(protocolParams, deviceTemplate->GetTemplate(), fwVersion, false);
        TRPCDeviceLoadRequest request(protocolParams, device, deviceTemplate, false);
        request.Parameters.insert("p1");
        auto registerList = request.GetParametersRegisterList(Json::Value(Json::objectValue));
        ASSERT_EQ(registerList.size(), 1u) << fwVersion;
        EXPECT_EQ(registerList.front().Id, "p1");
    }

    // the "p3" variants share the condition "mode==1", both match and form a chain
    {
        TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
        request.Parameters.insert("p3");
        Json::Value condParams(Json::objectValue);
        condParams["mode"] = 1;
        auto registerList = request.GetParametersRegisterList(condParams);
        ASSERT_EQ(registerList.size(), 1u);
        EXPECT_EQ(registerList.front().Id, "p3");
    }
}

/**
 * Checks that the register list for condition evaluation contains only parameters
 * referenced by conditions of the requested channels and parameters.
 */
TEST(TDeviceLoadTest, CollectsConditionDependenciesOfRequestedItemsOnly)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);

    TTemplateMap templateMap(GetTemplatesSchema());
    templateMap.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    TDeviceProtocolParams protocolParams = deviceFactory.GetProtocolParams("modbus");

    // the requested channel has no condition, nothing to read
    {
        auto deviceTemplate = templateMap.GetTemplate("device_load_conditions");
        TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
        request.Channels.insert("Always Visible");
        EXPECT_TRUE(request.GetConditionParametersRegisterList().empty());
    }

    // only the dependencies of the requested channel are read, not of every channel
    {
        auto deviceTemplate = templateMap.GetTemplate("device_load_conditions");
        TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
        request.Channels.insert("Mode Dependent");
        auto registerList = request.GetConditionParametersRegisterList();
        ASSERT_EQ(registerList.size(), 1u);
        EXPECT_EQ(registerList.front().Id, "mode");
    }

    // the dependencies of the requested parameter declarations are read
    {
        auto deviceTemplate = templateMap.GetTemplate("parameters_condition_variants");
        TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
        request.Parameters.insert("sp");
        auto registerList = request.GetConditionParametersRegisterList();
        ASSERT_EQ(registerList.size(), 1u);
        EXPECT_EQ(registerList.front().Id, "mode");
    }

    // read only parameters are read by device/Load, so their dependencies are read too, unlike device/Set
    {
        auto deviceTemplate = templateMap.GetTemplate("parameters_condition_variants");
        TRPCDeviceLoadRequest request(protocolParams, nullptr, deviceTemplate, false);
        request.Parameters.insert("ro");
        auto registerList = request.GetConditionParametersRegisterList();
        ASSERT_EQ(registerList.size(), 1u);
        EXPECT_EQ(registerList.front().Id, "romode");
    }
}
