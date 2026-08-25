#include "fake_serial_port.h"
#include "port/feature_port.h"
#include "rpc/rpc_device_set_task.h"
#include "rpc/rpc_exception.h"
#include "rpc/rpc_helpers.h"
#include "templates_map.h"
#include "test_utils.h"
#include <wblib/testing/testlog.h>

using namespace WBMQTT::Testing;

class TRPCDeviceSetTaskTest: public ::testing::Test
{
protected:
    TSerialDeviceFactory DeviceFactory;
    std::unique_ptr<TTemplateMap> Templates;

    void SetUp() override
    {
        RegisterProtocols(DeviceFactory);
        Templates = std::make_unique<TTemplateMap>(GetTemplatesSchema());
        Templates->AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);
    }

    PRPCDeviceSetRequest MakeRequest(const std::string& deviceType,
                                     const std::unordered_map<std::string, Json::Value>& parameters,
                                     const std::unordered_map<std::string, Json::Value>& channels = {})
    {
        auto request = std::make_shared<TRPCDeviceSetRequest>(DeviceFactory.GetProtocolParams("modbus"),
                                                              nullptr,
                                                              Templates->GetTemplate(deviceType),
                                                              false);
        for (const auto& item: parameters) {
            request->Parameters[item.first] = item.second;
        }
        for (const auto& item: channels) {
            request->Channels[item.first] = item.second;
        }
        return request;
    }

    //! Setup items of the request parameters selected by their conditions over conditionValues
    TDeviceSetupItems MakeParametersSetupItems(const std::string& deviceType,
                                               const std::unordered_map<std::string, Json::Value>& parameters,
                                               const Json::Value& conditionValues)
    {
        auto request = MakeRequest(deviceType, parameters);
        auto acting =
            request->SelectActingDeclarations("Parameter", request->GetParameterDeclarations(), conditionValues);
        return request->CreateSetupItems(Json::Value(Json::arrayValue), acting);
    }

    //! Setup items of the request channels selected by their conditions over conditionValues
    TDeviceSetupItems MakeChannelsSetupItems(const std::string& deviceType,
                                             const std::unordered_map<std::string, Json::Value>& channels,
                                             const Json::Value& conditionValues)
    {
        auto request = MakeRequest(deviceType, {}, channels);
        auto acting = request->SelectActingDeclarations("Channel", request->GetChannelDeclarations(), conditionValues);
        return request->CreateSetupItems(acting, Json::Value(Json::arrayValue));
    }

    //! Registers to read for the conditions of the request channels and parameters
    TRPCRegisterList MakeConditionParametersRegisterList(
        const std::string& deviceType,
        const std::unordered_map<std::string, Json::Value>& parameters,
        const std::unordered_map<std::string, Json::Value>& channels = {})
    {
        auto request = MakeRequest(deviceType, parameters, channels);
        return request->GetConditionParametersRegisterList(request->GetChannelDeclarations(),
                                                           request->GetParameterDeclarations());
    }
};

/**
 * Checks that parameter declarations are selected by their conditions over the condition
 * parameter values: only the declaration with a true condition acts, the value is converted
 * using its scale, a parameter without a matching declaration is skipped and is not an error.
 * Read only and unknown parameters of the request are not written and do not cause an error.
 */
TEST_F(TRPCDeviceSetTaskTest, SelectsParameterDeclarationByCondition)
{
    const std::unordered_map<std::string, Json::Value> parameters{{"sp", 100}, {"ro", 1}, {"unknown", 5}};
    Json::Value conditionValues;
    conditionValues["mode"] = 0;
    auto items = MakeParametersSetupItems("parameters_condition_variants", parameters, conditionValues);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ((*items.begin())->RawValue.Get<uint64_t>(), 100u);

    // The declaration acting in mode 1 has scale 0.1, so the same value gives another raw value
    conditionValues["mode"] = 1;
    items = MakeParametersSetupItems("parameters_condition_variants", parameters, conditionValues);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ((*items.begin())->RawValue.Get<uint64_t>(), 1000u);

    conditionValues["mode"] = 5;
    EXPECT_TRUE(MakeParametersSetupItems("parameters_condition_variants", parameters, conditionValues).empty());

    // the "sp" conditions compare "mode" with a number, they are false with the undefined value
    EXPECT_TRUE(MakeParametersSetupItems("parameters_condition_variants", parameters, Json::Value()).empty());

    // a parameter with a single declaration without a condition is written as before
    items = MakeParametersSetupItems("parameters_condition_variants", {{"mode", 1}}, Json::Value());
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ((*items.begin())->RawValue.Get<uint64_t>(), 1u);
}

/**
 * Checks that the register list for condition evaluation contains only parameters referenced
 * by conditions of the requested channels and parameters and not present in the request.
 */
TEST_F(TRPCDeviceSetTaskTest, GetConditionParametersRegisterList)
{
    auto registerList = MakeConditionParametersRegisterList("parameters_condition_variants", {{"sp", 100}});
    ASSERT_EQ(registerList.size(), 1u);
    EXPECT_EQ(registerList.front().Id, "mode");
    EXPECT_EQ(GetUint32RegisterAddress(registerList.front().Register->GetConfig()->GetAddress()), 1u);

    // "mode" is present in the request, nothing to read
    EXPECT_TRUE(
        MakeConditionParametersRegisterList("parameters_condition_variants", {{"sp", 100}, {"mode", 1}}).empty());

    // "mode" declarations have no conditions, nothing to read
    EXPECT_TRUE(MakeConditionParametersRegisterList("parameters_condition_variants", {{"mode", 1}}).empty());

    // conditions of read only declarations are not evaluated, their dependencies are not read
    EXPECT_TRUE(MakeConditionParametersRegisterList("parameters_condition_variants", {{"ro", 1}}).empty());

    // the conditions of the requested channels are collected too
    registerList = MakeConditionParametersRegisterList("parameters_condition_variants", {}, {{"Setpoint", 100}});
    ASSERT_EQ(registerList.size(), 1u);
    EXPECT_EQ(registerList.front().Id, "mode");
}

/**
 * Checks that the request schema requires numbers as parameter values: a string is not
 * a number for the evaluator and would silently be evaluated as an undefined value.
 */
TEST_F(TRPCDeviceSetTaskTest, RequiresNumericParameterValues)
{
    auto schema =
        LoadRPCRequestSchema(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-rpc-device-set-request.schema.json"),
                             "device/Set");

    Json::Value request;
    request["device_id"] = "test_1";
    request["parameters"]["mode"] = 1;
    EXPECT_NO_THROW(ValidateRPCRequest(request, schema));

    request["parameters"]["mode"] = "1";
    EXPECT_THROW(ValidateRPCRequest(request, schema), TRPCException);

    // channel values stay free-form, text channels are written as strings
    request.removeMember("parameters");
    request["channels"]["K1"] = "on";
    EXPECT_NO_THROW(ValidateRPCRequest(request, schema));
}

/**
 * Checks that parameters declared as a JSON object are matched by id too.
 */
TEST_F(TRPCDeviceSetTaskTest, WritesObjectFormParameters)
{
    auto items = MakeParametersSetupItems("parameters_object", {{"p1", 1}}, Json::Value());
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ((*items.begin())->RawValue.Get<uint64_t>(), 1u);
}

class TRPCDeviceSetTaskExecTest: public TLoggedFixture
{};

/**
 * Checks the whole device/Set request execution: the condition parameter missing from
 * the request is read from the device, the declarations of the parameter and the channel
 * are selected by their conditions and the values are written using the scale of the
 * acting declarations.
 */
TEST_F(TRPCDeviceSetTaskExecTest, ReadsConditionParameterAndWrites)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);
    TTemplateMap templates(GetTemplatesSchema());
    templates.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    auto protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templates.GetTemplate("parameters_condition_variants");
    auto config = std::make_shared<TDeviceConfig>("test", "1", "modbus");
    auto device = protocolParams.factory->CreateDevice(deviceTemplate->GetTemplate(), config, protocolParams.protocol);

    auto request = std::make_shared<TRPCDeviceSetRequest>(protocolParams, device, deviceTemplate, true);
    request->Parameters["sp"] = 100;
    request->Channels["Setpoint"] = 100;
    bool gotResult = false;
    std::string error;
    request->OnResult = [&](const Json::Value&) { gotResult = true; };
    request->OnError = [&](auto, const std::string& message) { error = message; };

    auto port = std::make_shared<TFakeSerialPort>(*this, "<parameters_condition_variants>");
    // "mode" is missing from the request, it is read from holding register 1, the device replies 1
    port->Expect({0x01, 0x03, 0x00, 0x01, 0x00, 0x01, 0xD5, 0xCA},
                 {0x01, 0x03, 0x02, 0x00, 0x01, 0x79, 0x84},
                 "read mode");
    // the mode 1 declaration of "sp" has scale 0.1, so 100 is written to holding register 2 as 1000
    port->Expect({0x01, 0x06, 0x00, 0x02, 0x03, 0xE8, 0x28, 0xB4},
                 {0x01, 0x06, 0x00, 0x02, 0x03, 0xE8, 0x28, 0xB4},
                 "write sp");
    // the mode 1 declaration of the "Setpoint" channel has scale 0.1 too, 100 is written to holding register 10
    port->Expect({0x01, 0x06, 0x00, 0x0A, 0x03, 0xE8, 0xA9, 0x76},
                 {0x01, 0x06, 0x00, 0x0A, 0x03, 0xE8, 0xA9, 0x76},
                 "write Setpoint");

    TSerialClientDeviceAccessHandler accessHandler(nullptr);
    TRPCDeviceSetSerialClientTask task(request);
    task.Run(std::make_shared<TFeaturePort>(port, false), accessHandler, {});

    EXPECT_TRUE(error.empty()) << error;
    EXPECT_TRUE(gotResult);
}

/**
 * Checks that a request is answered once and without an exchange when every parameter
 * is skipped: read only or not found in the template. An empty request takes the same path.
 */
TEST_F(TRPCDeviceSetTaskExecTest, SucceedsWhenEveryParameterIsSkipped)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);
    TTemplateMap templates(GetTemplatesSchema());
    templates.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    auto protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templates.GetTemplate("parameters_condition_variants");
    auto config = std::make_shared<TDeviceConfig>("test", "1", "modbus");
    auto device = protocolParams.factory->CreateDevice(deviceTemplate->GetTemplate(), config, protocolParams.protocol);

    auto request = std::make_shared<TRPCDeviceSetRequest>(protocolParams, device, deviceTemplate, true);
    request->Parameters["ro"] = 1;
    request->Parameters["unknown"] = 5;
    int results = 0;
    std::string error;
    request->OnResult = [&](const Json::Value&) { ++results; };
    request->OnError = [&](auto, const std::string& message) { error = message; };

    auto port = std::make_shared<TFakeSerialPort>(*this, "<parameters_condition_variants>");

    TSerialClientDeviceAccessHandler accessHandler(nullptr);
    TRPCDeviceSetSerialClientTask task(request);
    task.Run(std::make_shared<TFeaturePort>(port, false), accessHandler, {});

    EXPECT_TRUE(error.empty()) << error;
    EXPECT_EQ(results, 1);
}

/**
 * Checks the 0xFFFE marker handling in condition parameter reads: a WB device with
 * continuous read enabled answers 0xFFFE for an unsupported register instead of
 * a modbus exception. The suspicious register is re-read without continuous read,
 * the modbus exception confirms the device does not support it, so the value is
 * missing and the conditions over it are evaluated with an undefined value.
 * The request succeeds, the equality conditions of "sp" and "ov" are false,
 * both parameters are skipped and nothing is written. Before the fix 65534
 * entered condition evaluation, matched the "mode>0" declaration of "ov" and wrote it.
 */
TEST_F(TRPCDeviceSetTaskExecTest, SkipsParametersOnUnsupportedValueMarkerInConditions)
{
    TSerialDeviceFactory deviceFactory;
    RegisterProtocols(deviceFactory);
    TTemplateMap templates(GetTemplatesSchema());
    templates.AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);

    auto protocolParams = deviceFactory.GetProtocolParams("modbus");
    auto deviceTemplate = templates.GetTemplate("parameters_condition_variants");
    auto config = std::make_shared<TDeviceConfig>("test", "1", "modbus");
    auto device = protocolParams.factory->CreateDevice(deviceTemplate->GetTemplate(), config, protocolParams.protocol);
    // the 0xFFFE marker check runs for Wiren Board devices only
    device->SetWbDevice(true);

    auto request = std::make_shared<TRPCDeviceSetRequest>(protocolParams, device, deviceTemplate, true);
    request->Parameters["sp"] = 100;
    request->Parameters["ov"] = 1;
    bool gotResult = false;
    std::string error;
    request->OnResult = [&](const Json::Value&) { gotResult = true; };
    request->OnError = [&](auto, const std::string& message) { error = message; };

    auto port = std::make_shared<TFakeSerialPort>(*this, "<parameters_condition_variants>");
    // a Wiren Board device firmware version is requested on session preparation
    port->Expect({0x01, 0x03, 0x00, 0xFA, 0x00, 0x10, 0x64, 0x37}, {0x01, 0x83, 0x02, 0xC0, 0xF1}, "read fw version");
    // "mode" is read from holding register 1, the device replies 0xFFFE, the unsupported register marker
    port->Expect({0x01, 0x03, 0x00, 0x01, 0x00, 0x01, 0xD5, 0xCA},
                 {0x01, 0x03, 0x02, 0xFF, 0xFE, 0x78, 0x34},
                 "read mode");
    // the marker makes the register suspicious, it is re-read without continuous read
    port->Expect({0x01, 0x06, 0x00, 0x72, 0x00, 0x00, 0x29, 0xD1},
                 {0x01, 0x06, 0x00, 0x72, 0x00, 0x00, 0x29, 0xD1},
                 "disable continuous read");
    // the modbus exception confirms the device does not support the register
    port->Expect({0x01, 0x03, 0x00, 0x01, 0x00, 0x01, 0xD5, 0xCA}, {0x01, 0x83, 0x02, 0xC0, 0xF1}, "re-read mode");
    port->Expect({0x01, 0x06, 0x00, 0x72, 0x00, 0x01, 0xE8, 0x11},
                 {0x01, 0x06, 0x00, 0x72, 0x00, 0x01, 0xE8, 0x11},
                 "enable continuous read");

    TSerialClientDeviceAccessHandler accessHandler(nullptr);
    TRPCDeviceSetSerialClientTask task(request);
    task.Run(std::make_shared<TFeaturePort>(port, false), accessHandler, {});

    // the request succeeds, no write frames are expected
    EXPECT_TRUE(error.empty()) << error;
    EXPECT_TRUE(gotResult);
}

/**
 * Checks that a parameter with several declarations with simultaneously true conditions
 * is rejected: such declarations are a template error, config file validation reports them
 * as a duplicate definition of the parameter.
 */
TEST_F(TRPCDeviceSetTaskTest, RejectsParameterWithSeveralActiveDeclarations)
{
    Json::Value conditionValues;
    conditionValues["mode"] = 1;
    try {
        MakeParametersSetupItems("parameters_condition_variants", {{"ov", 1}}, conditionValues);
        ADD_FAILURE() << "Expect TRPCException";
    } catch (const TRPCException& e) {
        EXPECT_EQ(std::string(e.what()),
                  "Parameter \"ov\" is ambiguous: several declarations match the condition parameter values");
    }

    // condition values matching only one of the declarations are accepted
    conditionValues["mode"] = 2;
    auto items = MakeParametersSetupItems("parameters_condition_variants", {{"ov", 1}}, conditionValues);
    EXPECT_EQ(items.size(), 1u);
}

/**
 * Checks that channel declarations are selected by their conditions like parameters:
 * the value is converted using the scale of the acting declaration, a channel without
 * a matching declaration is skipped and is not an error.
 */
TEST_F(TRPCDeviceSetTaskTest, SelectsChannelDeclarationByCondition)
{
    Json::Value conditionValues;
    conditionValues["mode"] = 0;
    auto items = MakeChannelsSetupItems("parameters_condition_variants", {{"Setpoint", 100}}, conditionValues);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ((*items.begin())->RawValue.Get<uint64_t>(), 100u);

    conditionValues["mode"] = 1;
    items = MakeChannelsSetupItems("parameters_condition_variants", {{"Setpoint", 100}}, conditionValues);
    ASSERT_EQ(items.size(), 1u);
    EXPECT_EQ((*items.begin())->RawValue.Get<uint64_t>(), 1000u);

    conditionValues["mode"] = 5;
    EXPECT_TRUE(MakeChannelsSetupItems("parameters_condition_variants", {{"Setpoint", 100}}, conditionValues).empty());
}

/**
 * Checks that a channel with several simultaneously matching declarations is rejected
 * like a parameter, and that a channel not found in the template or read only is still rejected.
 */
TEST_F(TRPCDeviceSetTaskTest, RejectsAmbiguousAndUnknownChannels)
{
    Json::Value conditionValues;
    conditionValues["mode"] = 1;
    try {
        MakeChannelsSetupItems("parameters_condition_variants", {{"Level", 1}}, conditionValues);
        ADD_FAILURE() << "Expect TRPCException";
    } catch (const TRPCException& e) {
        EXPECT_EQ(std::string(e.what()),
                  "Channel \"Level\" is ambiguous: several declarations match the condition parameter values");
    }

    EXPECT_THROW(MakeRequest("parameters_condition_variants", {}, {{"Unknown", 1}})->GetChannelDeclarations(),
                 TRPCException);

    // "Temperature" is an input register, read only by the register type without a "readonly" key
    EXPECT_THROW(MakeRequest("parameters_condition_variants", {}, {{"Temperature", 1}})->GetChannelDeclarations(),
                 TRPCException);
}
