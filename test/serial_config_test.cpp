#include <wblib/testing/testlog.h>

#include "confed_schema_generator.h"
#include "config_merge_template.h"
#include "config_schema_generator.h"
#include "fake_serial_device.h"
#include "file_utils.h"
#include "serial_config.h"
#include "serial_device.h"
#include "test_utils.h"

using namespace std;
using namespace WBMQTT;
using namespace WBMQTT::Testing;

class TConfigParserTest: public TLoggedFixture
{
protected:
    TSerialDeviceFactory DeviceFactory;

    void SetUp()
    {
        RegisterProtocols(DeviceFactory);
        TFakeSerialDevice::Register(DeviceFactory);
    }

    void PrintConfig(PHandlerConfig config)
    {
        Emit() << "Debug: " << config->Debug;
        Emit() << "PublishParams: " << config->PublishParameters.Policy << " "
               << config->PublishParameters.PublishUnchangedInterval.count();
        Emit() << "Ports:";
        for (auto port_config: config->PortConfigs) {
            TTestLogIndent indent(*this);
            Emit() << "------";
            Emit() << "ConnSettings: " << port_config->Port->GetDescription();
            Emit() << "ConnectionMaxFailCycles: " << port_config->OpenCloseSettings.ConnectionMaxFailCycles;
            Emit() << "MaxFailTime: " << port_config->OpenCloseSettings.MaxFailTime.count();
            Emit() << "PollInterval: " << port_config->PollInterval.count();
            Emit() << "GuardInterval: " << port_config->RequestDelay.count();
            Emit() << "Response timeout: " << port_config->ResponseTimeout.count();

            if (port_config->Devices.empty()) {
                Emit() << "No device configs.";
                continue;
            }
            Emit() << "DeviceConfigs:";
            for (auto device: port_config->Devices) {
                auto device_config = device->DeviceConfig();
                TTestLogIndent indent(*this);
                Emit() << "------";
                Emit() << "Id: " << device_config->Id;
                Emit() << "Name: " << device_config->Name;
                Emit() << "SlaveId: " << device_config->SlaveId;
                Emit() << "MaxRegHole: " << device_config->MaxRegHole;
                Emit() << "MaxBitHole: " << device_config->MaxBitHole;
                Emit() << "MaxReadRegisters: " << device_config->MaxReadRegisters;
                Emit() << "GuardInterval: " << device_config->RequestDelay.count();
                Emit() << "DeviceTimeout: " << device_config->DeviceTimeout.count();
                Emit() << "Response timeout: " << device_config->ResponseTimeout.count();
                Emit() << "Frame timeout: " << device_config->FrameTimeout.count();
                if (!device_config->DeviceChannelConfigs.empty()) {
                    Emit() << "DeviceChannels:";
                    for (auto device_channel: device_config->DeviceChannelConfigs) {
                        TTestLogIndent indent(*this);
                        Emit() << "------";
                        Emit() << "Name: " << device_channel->GetName();
                        for (const auto it: device_channel->GetTitles()) {
                            if (it.first != "en") {
                                Emit() << "Name " << it.first << ": " << it.second;
                            }
                        }
                        Emit() << "Type: " << device_channel->Type;
                        Emit() << "MqttId: " << device_channel->MqttId;
                        Emit() << "DeviceId: " << device_channel->DeviceId;
                        Emit() << "Order: " << device_channel->Order;
                        Emit() << "OnValue: " << device_channel->OnValue;
                        if (!device_channel->OffValue.empty()) {
                            Emit() << "OffValue: " << device_channel->OffValue;
                        }
                        if (isnan(device_channel->Max)) {
                            Emit() << "Max: not set";
                        } else {
                            Emit() << "Max: " << device_channel->Max;
                        }
                        if (isnan(device_channel->Min)) {
                            Emit() << "Min: not set";
                        } else {
                            Emit() << "Min: " << device_channel->Min;
                        }
                        Emit() << "Precision: " << device_channel->Precision;
                        Emit() << "ReadOnly: " << device_channel->ReadOnly;
                        if (!device_channel->RegisterConfigs.empty()) {
                            Emit() << "Registers:";
                        }
                        for (auto reg: device_channel->RegisterConfigs) {
                            TTestLogIndent indent(*this);
                            Emit() << "------";
                            Emit() << "Type and Address: " << reg;
                            Emit() << "Format: " << RegisterFormatName(reg->Format);
                            Emit() << "Scale: " << reg->Scale;
                            Emit() << "Offset: " << reg->Offset;
                            Emit() << "RoundTo: " << reg->RoundTo;
                            Emit() << "Poll: " << reg->Poll;
                            Emit() << "ReadOnly: " << reg->ReadOnly;
                            Emit() << "TypeName: " << reg->TypeName;
                            Emit() << "PollInterval: " << reg->PollInterval.count();
                            if (reg->ErrorValue) {
                                Emit() << "ErrorValue: " << *reg->ErrorValue;
                            } else {
                                Emit() << "ErrorValue: not set";
                            }
                            if (reg->UnsupportedValue) {
                                Emit() << "UnsupportedValue: " << *reg->UnsupportedValue;
                            } else {
                                Emit() << "UnsupportedValue: not set";
                            }
                            Emit() << "WordOrder: " << reg->WordOrder;
                        }
                    }

                    if (device_config->SetupItemConfigs.empty())
                        continue;

                    Emit() << "SetupItems:";
                    for (auto setup_item: device_config->SetupItemConfigs) {
                        TTestLogIndent indent(*this);
                        Emit() << "------";
                        Emit() << "Name: " << setup_item->GetName();
                        Emit() << "Address: " << setup_item->GetRegisterConfig()->GetAddress();
                        Emit() << "Value: " << setup_item->GetValue();
                        Emit() << "RawValue: 0x" << std::setfill('0') << std::setw(2) << std::hex
                               << setup_item->GetRawValue();
                        Emit() << "Reg type: " << setup_item->GetRegisterConfig()->TypeName << " ("
                               << setup_item->GetRegisterConfig()->Type << ")";
                        Emit() << "Reg format: " << RegisterFormatName(setup_item->GetRegisterConfig()->Format);
                    }
                }
            }
        }
    }

    PHandlerConfig GetConfig(const std::string& filePath)
    {
        Json::Value configSchema = LoadConfigSchema(GetDataFilePath("../wb-mqtt-serial.schema.json"));
        TTemplateMap templateMap(
            GetDataFilePath("device-templates/"),
            LoadConfigTemplatesSchema(GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"), configSchema));

        return LoadConfig(GetDataFilePath(filePath), DeviceFactory, configSchema, templateMap);
    }
};

TEST_F(TConfigParserTest, Parse)
{
    PrintConfig(GetConfig("configs/parse_test.json"));
}

TEST_F(TConfigParserTest, SameSetupItems)
{
    // Check that setup registers in config have higher priority than setup registers with same addresses from template
    PrintConfig(GetConfig("configs/parse_test_setup.json"));
}

TEST_F(TConfigParserTest, UnsuccessfulParse)
{
    Json::Value configSchema = LoadConfigSchema(GetDataFilePath("../wb-mqtt-serial.schema.json"));
    TTemplateMap templateMap(
        GetDataFilePath("parser_test/templates/"),
        LoadConfigTemplatesSchema(GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"), configSchema));

    IterateDirByPattern(
        GetDataFilePath("configs/unsuccessful"),
        "unsuccessful-",
        [&](const std::string& fname) {
            Emit() << "Parsing config " << fname;
            try {
                PHandlerConfig config = LoadConfig(fname, DeviceFactory, configSchema, templateMap);
            } catch (const std::exception& e) {
                Emit() << e.what();
            }
            return false;
        },
        true);
}

TEST_F(TConfigParserTest, MergeDeviceConfigWithTemplate)
{
    Json::Value configSchema = LoadConfigSchema(GetDataFilePath("../wb-mqtt-serial.schema.json"));
    TTemplateMap templateMap(
        GetDataFilePath("parser_test/templates/"),
        LoadConfigTemplatesSchema(GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"), configSchema));

    for (auto i = 1; i <= 7; ++i) {
        auto deviceConfig(JSON::Parse(GetDataFilePath("parser_test/merge_template_ok" + to_string(i) + ".json")));
        std::string deviceType = deviceConfig.get("device_type", "").asString();
        auto mergedConfig(
            MergeDeviceConfigWithTemplate(deviceConfig, deviceType, templateMap.GetTemplate(deviceType).Schema));
        auto res(JSON::Parse(GetDataFilePath("parser_test/merge_template_res" + to_string(i) + ".json")));
        ASSERT_TRUE(JsonsMatch(res, mergedConfig)) << i;
    }
}

TEST_F(TConfigParserTest, ProtocolParametersSchemaRef)
{
    for (const auto& name: DeviceFactory.GetProtocolNames()) {
        ASSERT_FALSE(DeviceFactory.GetCommonDeviceSchemaRef(name).empty()) << name;
    }
}

class TConfedSchemaTest: public TLoggedFixture
{
protected:
    TSerialDeviceFactory DeviceFactory;
    Json::Value ConfigSchema;

    void SetUp()
    {
        ConfigSchema = LoadConfigSchema(GetDataFilePath("../wb-mqtt-serial.schema.json"));
        RegisterProtocols(DeviceFactory);
    }

    bool IncludesParameters(const Json::Value& v1, const Json::Value& v2)
    {
        for (auto lvl1 = v2.begin(); lvl1 != v2.end(); ++lvl1) {
            for (auto lvl2 = lvl1->begin(); lvl2 != lvl1->end(); ++lvl2) {
                if (v1[lvl1.name()][lvl2.name()] != *lvl2) {
                    return false;
                }
            }
        }
        return true;
    }
};

TEST_F(TConfedSchemaTest, PreserveSchemaTranslations)
{
    // Check that translations from wb-mqtt-serial.schema.json are not overwitten
    TTemplateMap templateMap(
        GetDataFilePath("translation-templates/templates1"),
        LoadConfigTemplatesSchema(GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"), ConfigSchema));

    ConfigSchema["translations"]["en"]["test translation"] = "test";
    ConfigSchema["translations"]["ru"]["test translation"] = "Тест";

    auto schema = MakeSchemaForConfed(ConfigSchema, templateMap, DeviceFactory);
    ASSERT_TRUE(IncludesParameters(schema["translations"], ConfigSchema["translations"]));
}

TEST_F(TConfedSchemaTest, MergeTranslations)
{
    // Remove all translations from wb-mqtt-serial.schema.json to check only generated translations
    ConfigSchema.removeMember("translations");
    for (size_t i = 1; i <= 2; ++i) {
        TTemplateMap templateMap(
            GetDataFilePath("translation-templates/templates" + to_string(i)),
            LoadConfigTemplatesSchema(GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"), ConfigSchema));
        auto schema = MakeSchemaForConfed(ConfigSchema, templateMap, DeviceFactory);

        auto tr(JSON::Parse(GetDataFilePath("translation-templates/tr" + to_string(i) + ".json")));
        ASSERT_TRUE(JsonsMatch(schema["translations"], tr)) << i;
    }
}
