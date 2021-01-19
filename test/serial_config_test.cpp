#include <wblib/testing/testlog.h>

#include "serial_device.h"
#include "serial_config.h"
#include "config_merge_template.h"
#include "file_utils.h"

using namespace std;
using namespace WBMQTT;
using namespace WBMQTT::Testing;


class TConfigParserTest: public TLoggedFixture 
{
    protected:
        TSerialDeviceFactory DeviceFactory;
};

TEST_F(TConfigParserTest, Parse)
{
    Json::Value configSchema = LoadConfigSchema(GetDataFilePath("../wb-mqtt-serial.schema.json"));
    TTemplateMap templateMap(GetDataFilePath("device-templates/"),
                             LoadConfigTemplatesSchema(GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"), 
                                                                             configSchema));

    PHandlerConfig config = LoadConfig(GetDataFilePath("configs/parse_test.json"), 
                                                       DeviceFactory,
                                                       configSchema,
                                                       templateMap);

    Emit() << "Debug: " << config->Debug;
    Emit() << "PublishParams: " << config->PublishParameters.Policy << " " << config->PublishParameters.PublishUnchangedInterval.count();
    Emit() << "Ports:";
    for (auto port_config: config->PortConfigs) {
        TTestLogIndent indent(*this);
        Emit() << "------";
        Emit() << "ConnSettings: " << port_config->Port->GetDescription();
        Emit() << "PollInterval: " << port_config->PollInterval.count();
        Emit() << "GuardInterval: " << port_config->RequestDelay.count();
        Emit() << "Response timeout: " << port_config->ResponseTimeout.count();

        if (port_config->DeviceConfigs.empty()) {
            Emit() << "No device configs.";
            continue;
        }
        Emit() << "DeviceConfigs:";
        for (auto device_config: port_config->DeviceConfigs) {
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
                    Emit() << "Name: " << device_channel->Name;
                    Emit() << "Type: " << device_channel->Type;
                    Emit() << "MqttId: " << device_channel->MqttId;
                    Emit() << "DeviceId: " << device_channel->DeviceId;
                    Emit() << "Order: " << device_channel->Order;
                    Emit() << "OnValue: " << device_channel->OnValue;
                    Emit() << "Max: " << device_channel->Max;
                    Emit() << "ReadOnly: " << device_channel->ReadOnly;
                    if (!device_channel->RegisterConfigs.empty()) {
                        Emit() << "Registers:";
                    }
                    for (auto reg: device_channel->RegisterConfigs) {
                        TTestLogIndent indent(*this);
                        Emit() << "------";
                        Emit() << "Type and Address: " << reg->TypeName << ": " << reg->Address;
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
                    Emit() << "Name: " << setup_item->Name;
                    Emit() << "Address: " << setup_item->RegisterConfig->Address;
                    Emit() << "Value: " << setup_item->Value;
                }
            }
        }
    }
}

TEST_F(TConfigParserTest, ForceDebug)
{
    Json::Value configSchema = LoadConfigSchema(GetDataFilePath("../wb-mqtt-serial.schema.json"));
    AddProtocolType(configSchema, "fake");
    AddRegisterType(configSchema, "fake");
    TTemplateMap t;
    PHandlerConfig Config = LoadConfig(GetDataFilePath("configs/config-test.json"), 
                                       DeviceFactory,
                                       configSchema, 
                                       t);
    ASSERT_TRUE(Config->Debug);
}

TEST_F(TConfigParserTest, UnsuccessfulParse)
{
    Json::Value configSchema = LoadConfigSchema(GetDataFilePath("../wb-mqtt-serial.schema.json"));
    TTemplateMap templateMap(GetDataFilePath("parser_test/templates/"),
                             LoadConfigTemplatesSchema(GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"), 
                                                                       configSchema));

    IterateDirByPattern(GetDataFilePath("configs/unsuccessful"),
                        "unsuccessful-",
                        [&](const std::string& fname)
                        {
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
    TTemplateMap templateMap(GetDataFilePath("parser_test/templates/"),
                             LoadConfigTemplatesSchema(GetDataFilePath("../wb-mqtt-serial-device-template.schema.json"), 
                                                                       configSchema));

    for (auto i = 1 ; i <= 7; ++i) {
        auto deviceConfig(JSON::Parse(GetDataFilePath("parser_test/merge_template_ok" + to_string(i) + ".json")));
        auto mergedConfig(MergeDeviceConfigWithTemplate(deviceConfig, templateMap));
        ASSERT_EQ(JSON::Parse(GetDataFilePath("parser_test/merge_template_res" + to_string(i) + ".json")), mergedConfig) << i;
    }
}