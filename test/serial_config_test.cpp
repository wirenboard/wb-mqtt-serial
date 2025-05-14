#include <algorithm>
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
    PRPCConfig RPCConfig = std::make_shared<TRPCConfig>();

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
            if (port_config->ReadRateLimit) {
                Emit() << "ReadRateLimit: " << port_config->ReadRateLimit->count();
            }
            Emit() << "GuardInterval: " << port_config->RequestDelay.count();
            Emit() << "Response timeout: " << port_config->ResponseTimeout.count();

            if (port_config->Devices.empty()) {
                Emit() << "No device configs.";
                continue;
            }
            Emit() << "DeviceConfigs:";
            for (auto device: port_config->Devices) {
                auto device_config = device->Device->DeviceConfig();
                TTestLogIndent indent(*this);
                Emit() << "------";
                Emit() << "Id: " << device_config->Id;
                Emit() << "Name: " << device_config->Name;
                Emit() << "SlaveId: " << device_config->SlaveId;
                Emit() << "MaxRegHole: " << device_config->MaxRegHole;
                Emit() << "MaxBitHole: " << device_config->MaxBitHole;
                Emit() << "MaxReadRegisters: " << device_config->MaxReadRegisters;
                Emit() << "MinReadRegisters: " << device_config->MinReadRegisters;
                Emit() << "GuardInterval: " << device_config->RequestDelay.count();
                Emit() << "DeviceTimeout: " << device_config->DeviceTimeout.count();
                Emit() << "Response timeout: " << device_config->ResponseTimeout.count();
                Emit() << "Frame timeout: " << device_config->FrameTimeout.count();
                if (!device->Channels.empty()) {
                    Emit() << "DeviceChannels:";
                    for (auto device_channel: device->Channels) {
                        TTestLogIndent indent(*this);
                        Emit() << "------";
                        Emit() << "Name: " << device_channel->GetName();
                        for (const auto& it: device_channel->GetTitles()) {
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
                        if (!device_channel->Registers.empty()) {
                            Emit() << "Registers:";
                        }
                        for (auto channelsRegister: device_channel->Registers) {
                            auto reg = channelsRegister->GetConfig();
                            TTestLogIndent indent(*this);
                            Emit() << "------";
                            Emit() << "Type and Address: " << reg;
                            Emit() << "Format: " << RegisterFormatName(reg->Format);
                            Emit() << "Scale: " << reg->Scale;
                            Emit() << "Offset: " << reg->Offset;
                            Emit() << "RoundTo: " << reg->RoundTo;
                            Emit() << "Poll: " << (reg->AccessType != TRegisterConfig::EAccessType::WRITE_ONLY);
                            Emit() << "ReadOnly: " << (reg->AccessType == TRegisterConfig::EAccessType::READ_ONLY);
                            Emit() << "TypeName: " << reg->TypeName;
                            if (reg->ReadPeriod) {
                                Emit() << "ReadPeriod: " << reg->ReadPeriod->count();
                            }
                            if (reg->ReadRateLimit) {
                                Emit() << "ReadRateLimit: " << reg->ReadRateLimit->count();
                            }
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

                    if (device->Device->GetSetupItems().empty())
                        continue;

                    Emit() << "SetupItems:";
                    for (auto setup_item: device->Device->GetSetupItems()) {
                        TTestLogIndent indent(*this);
                        Emit() << "------";
                        Emit() << "Name: " << setup_item->Name;
                        Emit() << "Address: " << setup_item->Register->GetConfig()->GetAddress();
                        Emit() << "Value: " << setup_item->HumanReadableValue;
                        if (setup_item->RawValue.GetType() == TRegisterValue::ValueType::String) {
                            Emit() << "RawValue: " << setup_item->RawValue;
                        } else {
                            Emit() << "RawValue: 0x" << std::setfill('0') << std::setw(2) << std::hex
                                   << setup_item->RawValue;
                        }
                        Emit() << "Reg type: " << setup_item->Register->GetConfig()->TypeName << " ("
                               << setup_item->Register->GetConfig()->Type << ")";
                        Emit() << "Reg format: " << RegisterFormatName(setup_item->Register->GetConfig()->Format);
                    }
                }
            }
        }
    }

    PHandlerConfig GetConfig(const std::string& filePath)
    {
        auto commonDeviceSchema(
            WBMQTT::JSON::Parse(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-confed-common.schema.json")));
        TTemplateMap templateMap(
            LoadConfigTemplatesSchema(GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"),
                                      commonDeviceSchema));
        templateMap.AddTemplatesDir(GetDataFilePath("device-templates/"));
        auto portsSchema(WBMQTT::JSON::Parse(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-ports.schema.json")));
        TProtocolConfedSchemasMap protocolSchemas(TLoggedFixture::GetDataFilePath("../protocols"), commonDeviceSchema);
        return LoadConfig(GetDataFilePath(filePath),
                          DeviceFactory,
                          commonDeviceSchema,
                          templateMap,
                          RPCConfig,
                          portsSchema,
                          protocolSchemas);
    }
};

TEST_F(TConfigParserTest, Parse)
{
    PrintConfig(GetConfig("configs/parse_test.json"));
}

TEST_F(TConfigParserTest, ParseRateLimit)
{
    PrintConfig(GetConfig("configs/parse_test_rate_limit.json"));
}

TEST_F(TConfigParserTest, SameSetupItems)
{
    // Check that setup registers in config have higher priority than setup registers with same addresses from template
    PrintConfig(GetConfig("configs/parse_test_setup.json"));
}

TEST_F(TConfigParserTest, ParametersAsArray)
{
    // Check loading device template with parameters defined as array
    PrintConfig(GetConfig("configs/parse_test_parameters_array.json"));
}

TEST_F(TConfigParserTest, SetupCondition)
{
    // Check loading device template with setup with condition
    PrintConfig(GetConfig("configs/parse_test_setup_condition.json"));
}

TEST_F(TConfigParserTest, UnsuccessfulParse)
{
    auto commonDeviceSchema(
        WBMQTT::JSON::Parse(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-confed-common.schema.json")));
    TTemplateMap templateMap(LoadConfigTemplatesSchema(GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"),
                                                       commonDeviceSchema));
    templateMap.AddTemplatesDir(GetDataFilePath("parser_test/templates/"));
    auto portsSchema(WBMQTT::JSON::Parse(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-ports.schema.json")));
    TProtocolConfedSchemasMap protocolSchemas(TLoggedFixture::GetDataFilePath("../protocols"), commonDeviceSchema);

    IterateDirByPattern(
        GetDataFilePath("configs/unsuccessful"),
        "unsuccessful-",
        [&](const std::string& fname) {
            Emit() << "Parsing config " << fname;
            try {
                PHandlerConfig config = LoadConfig(fname,
                                                   DeviceFactory,
                                                   commonDeviceSchema,
                                                   templateMap,
                                                   RPCConfig,
                                                   portsSchema,
                                                   protocolSchemas);
            } catch (const std::exception& e) {
                Emit() << e.what();
            }
            return false;
        },
        true);
}

TEST_F(TConfigParserTest, MergeDeviceConfigWithTemplate)
{
    auto commonDeviceSchema(
        WBMQTT::JSON::Parse(TLoggedFixture::GetDataFilePath("../wb-mqtt-serial-confed-common.schema.json")));
    TTemplateMap templateMap(LoadConfigTemplatesSchema(GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"),
                                                       commonDeviceSchema));
    templateMap.AddTemplatesDir(GetDataFilePath("parser_test/templates/"));

    for (auto i = 1; i <= 13; ++i) {
        auto deviceConfig(JSON::Parse(GetDataFilePath("parser_test/merge_template_ok" + to_string(i) + ".json")));
        std::string deviceType = deviceConfig.get("device_type", "").asString();
        auto mergedConfig(MergeDeviceConfigWithTemplate(deviceConfig,
                                                        deviceType,
                                                        templateMap.GetTemplate(deviceType)->GetTemplate()));
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

TEST_F(TConfigParserTest, ParseModbusDevideWithWriteAddress)
{
    auto portConfigs = GetConfig("configs/parse_test_modbus_write_address.json")->PortConfigs;
    EXPECT_FALSE(portConfigs.empty());
    auto devices = portConfigs[0]->Devices;
    EXPECT_FALSE(devices.empty());
    auto deviceChannels = devices[0]->Channels;
    EXPECT_FALSE(deviceChannels.empty());
    auto regs = deviceChannels[0]->Registers;
    EXPECT_FALSE(regs.empty());
    EXPECT_EQ(GetUint32RegisterAddress(regs[0]->GetConfig()->GetAddress()), 110);
    EXPECT_EQ(GetUint32RegisterAddress(regs[0]->GetConfig()->GetWriteAddress()), 115);
}

TEST_F(TConfigParserTest, ParseReadonlyParameters)
{
    auto portConfigs = GetConfig("configs/parse_test_readonly_parameters.json")->PortConfigs;
    EXPECT_FALSE(portConfigs.empty());
    auto devices = portConfigs[0]->Devices;
    EXPECT_FALSE(devices.empty());
    auto setupItems = devices[0]->Device->GetSetupItems();
    EXPECT_EQ(setupItems.size(), 1);
    EXPECT_EQ(setupItems[0]->Name, "p2");
}

TEST_F(TConfigParserTest, ParseEnum)
{
    auto portConfigs = GetConfig("configs/parse_enum.json")->PortConfigs;
    EXPECT_FALSE(portConfigs.empty());
    auto devices = portConfigs[0]->Devices;
    EXPECT_FALSE(devices.empty());
    auto deviceChannels = devices[0]->Channels;
    EXPECT_FALSE(deviceChannels.empty());
    auto titles1 = deviceChannels[0]->GetEnumTitles();
    EXPECT_EQ(titles1.size(), 2);
    EXPECT_EQ(titles1["0"]["en"], "zero");
    EXPECT_EQ(titles1["1"]["en"], "one");
    auto titles2 = deviceChannels[1]->GetEnumTitles();
    EXPECT_EQ(titles2.size(), 2);
    EXPECT_EQ(titles2["2"]["en"], "two");
    EXPECT_EQ(titles2["3"]["en"], "three");
}
