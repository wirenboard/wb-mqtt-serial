#include <map>
#include <memory>
#include <algorithm>
#include <cassert>
#include <gtest/gtest.h>

#include "testlog.h"
#include "fake_modbus.h"
#include "fake_mqtt.h"
#include "../modbus_config.h"
#include "../modbus_observer.h"

class TModbusClientTest: public TLoggedFixture
{
protected:
    void SetUp();
    void TearDown();
    PFakeModbusConnector Connector;
    PModbusClient ModbusClient;
    PFakeSlave Slave;
};

void TModbusClientTest::SetUp()
{
    TModbusConnectionSettings settings(TFakeModbusConnector::PORT0, 115200, 'N', 8, 1);
    Connector = PFakeModbusConnector(new TFakeModbusConnector(*this));
    ModbusClient = PModbusClient(new TModbusClient(settings, Connector));
    ModbusClient->SetCallback([this](std::shared_ptr<TModbusRegister> reg) {
            Emit() << "Modbus Callback: " << reg->ToString() << " becomes " <<
                ModbusClient->GetTextValue(reg);
        });
    ModbusClient->SetErrorCallback([this](std::shared_ptr<TModbusRegister> reg) {
            string error = (reg->ErrorMessage == "Poll") ? "read" : "write";
            Emit() << "Modbus ErrorCallback: " << reg->ToString() << " gets " <<
                error << " error";
        });
    Slave = Connector->AddSlave(TFakeModbusConnector::PORT0, 1,
                                TRegisterRange(0, 10),
                                TRegisterRange(10, 20),
                                TRegisterRange(20, 30),
                                TRegisterRange(30, 40));
}

void TModbusClientTest::TearDown()
{
    ModbusClient.reset();
    Connector.reset();
    TLoggedFixture::TearDown();
}

TEST_F(TModbusClientTest, Poll)
{
    std::shared_ptr<TModbusRegister> coil0(new TModbusRegister(1, TModbusRegister::COIL, 0));
    std::shared_ptr<TModbusRegister> coil1(new TModbusRegister(1, TModbusRegister::COIL, 1));
    std::shared_ptr<TModbusRegister> discrete10(new TModbusRegister(1, TModbusRegister::DISCRETE_INPUT, 10));
    std::shared_ptr<TModbusRegister> holding22(new TModbusRegister(1, TModbusRegister::HOLDING_REGISTER, 22));
    std::shared_ptr<TModbusRegister> input33(new TModbusRegister(1, TModbusRegister::INPUT_REGISTER, 33));

    ModbusClient->AddRegister(coil0);
    ModbusClient->AddRegister(coil1);
    ModbusClient->AddRegister(discrete10);
    ModbusClient->AddRegister(holding22);
    ModbusClient->AddRegister(input33);

    ModbusClient->Connect();

    Note() << "Cycle()";
    ModbusClient->Cycle();

    Slave->Coils[1] = 1;
    Slave->Discrete[10] = 1;
    Slave->Holding[22] = 4242;
    Slave->Input[33] = 42000;

    Note() << "Cycle()";
    ModbusClient->Cycle();

    EXPECT_EQ(0, ModbusClient->GetRawValue(coil0));
    EXPECT_EQ(1, ModbusClient->GetRawValue(coil1));
    EXPECT_EQ(1, ModbusClient->GetRawValue(discrete10));
    EXPECT_EQ(4242, ModbusClient->GetRawValue(holding22));
    EXPECT_EQ(42000, ModbusClient->GetRawValue(input33));
}

TEST_F(TModbusClientTest, Write)
{
    std::shared_ptr<TModbusRegister> coil1(new TModbusRegister(1, TModbusRegister::COIL, 1));
    std::shared_ptr<TModbusRegister> holding20(new TModbusRegister(1, TModbusRegister::HOLDING_REGISTER, 20));
    ModbusClient->AddRegister(coil1);
    ModbusClient->AddRegister(holding20);

    Note() << "Cycle()";
    ModbusClient->Cycle();

    ModbusClient->SetTextValue(coil1, "1");
    ModbusClient->SetTextValue(holding20, "4242");

    for (int i = 0; i < 3; ++i) {
        Note() << "Cycle()";
        ModbusClient->Cycle();

        EXPECT_EQ(1, ModbusClient->GetRawValue(coil1));
        EXPECT_EQ(1, Slave->Coils[1]);
        EXPECT_EQ(4242, ModbusClient->GetRawValue(holding20));
        EXPECT_EQ(4242, Slave->Holding[20]);
    }
}

TEST_F(TModbusClientTest, S8)
{
    std::shared_ptr<TModbusRegister> holding20 (new TModbusRegister(1, TModbusRegister::HOLDING_REGISTER, 20, TModbusRegister::S8));
    std::shared_ptr<TModbusRegister> input30(new TModbusRegister(1, TModbusRegister::INPUT_REGISTER, 30, TModbusRegister::S8));
    ModbusClient->AddRegister(holding20);
    ModbusClient->AddRegister(input30);

    Note() << "server -> client: 10, 20";
    Slave->Holding[20] = 10;
    Slave->Input[30] = 20;
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(10, ModbusClient->GetRawValue(holding20));
    EXPECT_EQ(20, ModbusClient->GetRawValue(input30));

    Note() << "server -> client: -2, -3";
    Slave->Holding[20] = 254;
    Slave->Input[30] = 253;
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(-2, ModbusClient->GetRawValue(holding20));
    EXPECT_EQ(-3, ModbusClient->GetRawValue(input30));

    Note() << "client -> server: 10";
    ModbusClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(10, ModbusClient->GetRawValue(holding20));
    EXPECT_EQ(10, Slave->Holding[20]);

    Note() << "client -> server: -2";
    ModbusClient->SetTextValue(holding20, "-2");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(-2, ModbusClient->GetRawValue(holding20));
    EXPECT_EQ(254, Slave->Holding[20]);
}

TEST_F(TModbusClientTest, S64)
{
    std::shared_ptr<TModbusRegister> holding20 (new TModbusRegister(1, TModbusRegister::HOLDING_REGISTER, 20, TModbusRegister::S64));
    std::shared_ptr<TModbusRegister> input30(new TModbusRegister(1, TModbusRegister::INPUT_REGISTER, 30, TModbusRegister::S64));
    ModbusClient->AddRegister(holding20);
    ModbusClient->AddRegister(input30);

    Note() << "server -> client: 10, 20";
    Slave->Holding[20] = 0x00AA;
    Slave->Holding[21] = 0x00BB;
    Slave->Holding[22] = 0x00CC;
    Slave->Holding[23] = 0x00DD;
    Slave->Input[30] = 0xFFFF;
    Slave->Input[31] = 0xFFFF;
    Slave->Input[32] = 0xFFFF;
    Slave->Input[33] = 0xFFFF;
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(0x00AA00BB00CC00DD, ModbusClient->GetRawValue(holding20));
    EXPECT_EQ(0xFFFFFFFFFFFFFFFF, ModbusClient->GetRawValue(input30));

    Note() << "client -> server: 10";
    ModbusClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(10, ModbusClient->GetRawValue(holding20));
    EXPECT_EQ(0, Slave->Holding[20]);
    EXPECT_EQ(0, Slave->Holding[21]);
    EXPECT_EQ(0, Slave->Holding[22]);
    EXPECT_EQ(10, Slave->Holding[23]);


    Note() << "client -> server: -2";
    ModbusClient->SetTextValue(holding20, "-2");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(-2, ModbusClient->GetRawValue(holding20));
    EXPECT_EQ(0xFFFF, Slave->Holding[20]);
    EXPECT_EQ(0xFFFF, Slave->Holding[21]);
    EXPECT_EQ(0xFFFF, Slave->Holding[22]);
    EXPECT_EQ(0xFFFE, Slave->Holding[23]);

}

TEST_F(TModbusClientTest, ReadErrors)
{
    std::shared_ptr<TModbusRegister> holding200(new TModbusRegister(1, TModbusRegister::HOLDING_REGISTER, 200));
    ModbusClient->AddRegister(holding200);
    Note() << "Cycle()";
    ModbusClient->Cycle();
}

class TConfigParserTest: public TLoggedFixture {};

TEST_F(TConfigParserTest, Parse)
{
    TConfigTemplateParser device_parser(GetDataFilePath("../wb-homa-modbus-templates/"),false);
    TConfigParser parser(GetDataFilePath("../config.json"), false, device_parser.Parse());
    //TConfigParser parser(GetDataFilePath("../config-test.json"), false);
    PHandlerConfig config = parser.Parse();
    Emit() << "Debug: " << config->Debug;
    Emit() << "Ports:";
    for (auto port_config: config->PortConfigs) {
        TTestLogIndent indent(*this);
        ASSERT_EQ(config->Debug, port_config->Debug);
        Emit() << "------";
        Emit() << "ConnSettings: " << port_config->ConnSettings;
        Emit() << "PollInterval: " << port_config->PollInterval;
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
            if (!device_config->ModbusChannels.empty()) {
                Emit() << "ModbusChannels:";
                for (auto modbus_channel: device_config->ModbusChannels) {
                    TTestLogIndent indent(*this);
                    Emit() << "------";
                    Emit() << "Name: " << modbus_channel->Name;
                    Emit() << "Type: " << modbus_channel->Type;
                    Emit() << "DeviceId: " << modbus_channel->DeviceId;
                    Emit() << "Order: " << modbus_channel->Order;
                    Emit() << "OnValue: " << modbus_channel->OnValue;
                    Emit() << "Max: " << modbus_channel->Max;
                    Emit() << "ReadOnly: " << modbus_channel->ReadOnly;
                    std::stringstream s;
                    bool first = true;
                    for (auto reg: modbus_channel->Registers) {
                        if (first)
                            first = false;
                        else
                            s << ", ";
                        s << reg;
                    }
                    Emit() << "Registers: " << s.str();
                }

                if (device_config->SetupItems.empty())
                    continue;

                Emit() << "SetupItems:";
                for (auto setup_item: device_config->SetupItems) {
                    TTestLogIndent indent(*this);
                    Emit() << "------";
                    Emit() << "Name: " << setup_item->Name;
                    Emit() << "Address: " << setup_item->Address;
                    Emit() << "Value: " << setup_item->Value;
                }
            }
        }
    }

}

TEST_F(TConfigParserTest, ForceDebug)
{
    TConfigParser parser(GetDataFilePath("../config-test.json"), true);
    PHandlerConfig config = parser.Parse();
    ASSERT_TRUE(config->Debug);
}

class TModbusDeviceTest: public TLoggedFixture
{
protected:
    void SetUp();
    void FilterConfig(const std::string& device_name);
    PFakeModbusConnector Connector;
    PHandlerConfig Config;
    PFakeMQTTClient MQTTClient;
};

void TModbusDeviceTest::SetUp()
{
    Connector = PFakeModbusConnector(new TFakeModbusConnector(*this));
    TConfigParser parser(GetDataFilePath("../config-test.json"), false);
    Config = parser.Parse();
    MQTTClient = PFakeMQTTClient(new TFakeMQTTClient("modbus-test", *this));
}

void TModbusDeviceTest::FilterConfig(const std::string& device_name)
{
    for (auto port_config: Config->PortConfigs) {
        port_config->DeviceConfigs.erase(
            remove_if(port_config->DeviceConfigs.begin(),
                      port_config->DeviceConfigs.end(),
                      [device_name](PDeviceConfig device_config) {
                          return device_config->Name != device_name;
                      }),
            port_config->DeviceConfigs.end());
    }
    Config->PortConfigs.erase(
        remove_if(Config->PortConfigs.begin(),
                  Config->PortConfigs.end(),
                  [](PPortConfig port_config) {
                      return port_config->DeviceConfigs.empty();
                  }),
        Config->PortConfigs.end());
    ASSERT_FALSE(Config->PortConfigs.empty()) << "device not found: " << device_name;
}

TEST_F(TModbusDeviceTest, DDL24)
{
    FilterConfig("DDL24");
    PFakeSlave slave = Connector->AddSlave(TFakeModbusConnector::PORT0,
                                           Config->PortConfigs[0]->DeviceConfigs[0]->SlaveId,
                                           TRegisterRange(),
                                           TRegisterRange(),
                                           TRegisterRange(4, 19),
                                           TRegisterRange());

    PMQTTModbusObserver modbus_observer(new TMQTTModbusObserver(MQTTClient, Config, Connector));
    modbus_observer->SetUp();

    Note() << "ModbusLoopOnce()";
    modbus_observer->ModbusLoopOnce();

    MQTTClient->DoPublish(true, 0, "/devices/ddl24/controls/RGB/on", "10;20;30");

    Note() << "ModbusLoopOnce()";
    modbus_observer->ModbusLoopOnce();
    ASSERT_EQ(10, slave->Holding[4]);
    ASSERT_EQ(20, slave->Holding[5]);
    ASSERT_EQ(30, slave->Holding[6]);

    slave->Holding[4] = 32;
    slave->Holding[5] = 64;
    slave->Holding[6] = 128;
    Note() << "ModbusLoopOnce() after slave update";
    modbus_observer->ModbusLoopOnce();
}

// TBD: the code must check mosquitto return values
