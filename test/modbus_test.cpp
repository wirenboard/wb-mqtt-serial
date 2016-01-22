#include <map>
#include <memory>
#include <algorithm>
#include <cassert>
#include <gtest/gtest.h>

#include "testlog.h"
#include "fake_mqtt.h"
#include "modbus_config.h"
#include "modbus_observer.h"
#include "modbus_server.h"
#include "serial_protocol.h"
#include "pty_based_fake_serial.h"

class TModbusTestBase: public TLoggedFixture
{
protected:
    void SetUp();
    void TearDown();
    PPtyBasedFakeSerial FakeSerial;
    PSerialPort ServerSerial;
    PModbusServer ModbusServer;
};

void TModbusTestBase::SetUp()
{
    TLoggedFixture::SetUp();
    FakeSerial = std::make_shared<TPtyBasedFakeSerial>(*this);
    FakeSerial->StartForwarding();
    ServerSerial = std::make_shared<TSerialPort>(
        TSerialPortSettings(FakeSerial->GetSecondaryPtsName(), 9600, 'N', 8, 1, 10000));
#if 0
    ServerSerial->SetDebug(true);
#endif
    ModbusServer = std::make_shared<TModbusServer>(ServerSerial);
}

void TModbusTestBase::TearDown()
{
    FakeSerial.reset();
    TLoggedFixture::TearDown();
}

class TModbusClientTest: public TModbusTestBase
{
protected:
    void SetUp();
    void TearDown();
    PModbusClient ModbusClient;
    PModbusSlave Slave;
};

void TModbusClientTest::SetUp()
{
    TModbusTestBase::SetUp();

    Slave = ModbusServer->SetSlave(
        1, TModbusRange(
            TRegisterRange(0, 10),
            TRegisterRange(10, 20),
            TRegisterRange(20, 30),
            TRegisterRange(30, 40)));
    ModbusServer->Start();

    ModbusClient = std::make_shared<TModbusClient>(
        TSerialPortSettings(FakeSerial->GetPrimaryPtsName(), 9600, 'N', 8, 1));
#if 0
    ModbusClient->SetModbusDebug(true);
#endif
    ModbusClient->SetCallback([this](PModbusRegister reg) {
            Emit() << "Modbus Callback: " << reg->ToString() << " becomes " <<
                ModbusClient->GetTextValue(reg);
        });
    ModbusClient->SetErrorCallback(
        [this](PModbusRegister reg, TModbusClient::TErrorState errorState) {
            const char* what;
            switch (errorState) {
            case TModbusClient::WriteError:
                what = "write error";
                break;
            case TModbusClient::ReadError:
                what = "read error";
                break;
            case TModbusClient::ReadWriteError:
                what = "read+write error";
                break;
            default:
                what = "no error";
            }
            Emit() << "Modbus ErrorCallback: " << reg->ToString() << ": " << what;
        });
}

void TModbusClientTest::TearDown()
{
    ModbusClient.reset();
    TModbusTestBase::TearDown();
}

TEST_F(TModbusClientTest, Poll)
{
    PModbusRegister coil0(new TModbusRegister(1, TModbusRegister::COIL, 0));
    PModbusRegister coil1(new TModbusRegister(1, TModbusRegister::COIL, 1));
    PModbusRegister discrete10(new TModbusRegister(1, TModbusRegister::DISCRETE_INPUT, 10));
    PModbusRegister holding22(new TModbusRegister(1, TModbusRegister::HOLDING_REGISTER, 22));
    PModbusRegister input33(new TModbusRegister(1, TModbusRegister::INPUT_REGISTER, 33));

    ModbusClient->AddRegister(coil0);
    ModbusClient->AddRegister(coil1);
    ModbusClient->AddRegister(discrete10);
    ModbusClient->AddRegister(holding22);
    ModbusClient->AddRegister(input33);

    Note() << "Cycle()";
    ModbusClient->Cycle();

    Slave->Coils[1] = 1;
    Slave->Discrete[10] = 1;
    Slave->Holding[22] = 4242;
    Slave->Input[33] = 42000;

    FakeSerial->Flush();
    Note() << "Cycle()";
    ModbusClient->Cycle();

    EXPECT_EQ(to_string(0), ModbusClient->GetTextValue(coil0));
    EXPECT_EQ(to_string(1), ModbusClient->GetTextValue(coil1));
    EXPECT_EQ(to_string(1), ModbusClient->GetTextValue(discrete10));
    EXPECT_EQ(to_string(4242), ModbusClient->GetTextValue(holding22));
    EXPECT_EQ(to_string(42000), ModbusClient->GetTextValue(input33));
}

TEST_F(TModbusClientTest, Write)
{
    PModbusRegister coil1(new TModbusRegister(1, TModbusRegister::COIL, 1));
    PModbusRegister holding20(new TModbusRegister(1, TModbusRegister::HOLDING_REGISTER, 20));
    ModbusClient->AddRegister(coil1);
    ModbusClient->AddRegister(holding20);

    Note() << "Cycle()";
    ModbusClient->Cycle();

    ModbusClient->SetTextValue(coil1, "1");
    ModbusClient->SetTextValue(holding20, "4242");

    for (int i = 0; i < 3; ++i) {
        FakeSerial->Flush();
        Note() << "Cycle()";
        ModbusClient->Cycle();

        EXPECT_EQ(to_string(1), ModbusClient->GetTextValue(coil1));
        EXPECT_EQ(1, Slave->Coils[1]);
        EXPECT_EQ(to_string(4242), ModbusClient->GetTextValue(holding20));
        EXPECT_EQ(4242, Slave->Holding[20]);
    }
}

TEST_F(TModbusClientTest, S8)
{
    PModbusRegister holding20 (new TModbusRegister(1, TModbusRegister::HOLDING_REGISTER, 20, S8));
    PModbusRegister input30(new TModbusRegister(1, TModbusRegister::INPUT_REGISTER, 30, S8));
    ModbusClient->AddRegister(holding20);
    ModbusClient->AddRegister(input30);

    Note() << "server -> client: 10, 20";
    Slave->Holding[20] = 10;
    Slave->Input[30] = 20;
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(10), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(to_string(20), ModbusClient->GetTextValue(input30));

    FakeSerial->Flush();
    Note() << "server -> client: -2, -3";
    Slave->Holding[20] = 254;
    Slave->Input[30] = 253;
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(-2), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(to_string(-3), ModbusClient->GetTextValue(input30));

    FakeSerial->Flush();
    Note() << "client -> server: 10";
    ModbusClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(10), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(10, Slave->Holding[20]);

    FakeSerial->Flush();
    Note() << "client -> server: -2";
    ModbusClient->SetTextValue(holding20, "-2");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(-2), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(254, Slave->Holding[20]);
}


TEST_F(TModbusClientTest, S64)
{
    PModbusRegister holding20 (new TModbusRegister(1, TModbusRegister::HOLDING_REGISTER, 20, S64));
    PModbusRegister input30(new TModbusRegister(1, TModbusRegister::INPUT_REGISTER, 30, S64));
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
    EXPECT_EQ(to_string(0x00AA00BB00CC00DD), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(to_string(-1), ModbusClient->GetTextValue(input30));

    FakeSerial->Flush();
    Note() << "client -> server: 10";
    ModbusClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(10), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(0, Slave->Holding[20]);
    EXPECT_EQ(0, Slave->Holding[21]);
    EXPECT_EQ(0, Slave->Holding[22]);
    EXPECT_EQ(10, Slave->Holding[23]);

    FakeSerial->Flush();
    Note() << "client -> server: -2";
    ModbusClient->SetTextValue(holding20, "-2");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(-2), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(0xFFFF, Slave->Holding[20]);
    EXPECT_EQ(0xFFFF, Slave->Holding[21]);
    EXPECT_EQ(0xFFFF, Slave->Holding[22]);
    EXPECT_EQ(0xFFFE, Slave->Holding[23]);
}

TEST_F(TModbusClientTest, U64)
{
    PModbusRegister holding20 (new TModbusRegister(1, TModbusRegister::HOLDING_REGISTER, 20, U64));
    PModbusRegister input30(new TModbusRegister(1, TModbusRegister::INPUT_REGISTER, 30, U64));
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
    EXPECT_EQ(to_string(0x00AA00BB00CC00DD), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ("18446744073709551615", ModbusClient->GetTextValue(input30));

    FakeSerial->Flush();
    Note() << "client -> server: 10";
    ModbusClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(10), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(0, Slave->Holding[20]);
    EXPECT_EQ(0, Slave->Holding[21]);
    EXPECT_EQ(0, Slave->Holding[22]);
    EXPECT_EQ(10, Slave->Holding[23]);

    FakeSerial->Flush();
    Note() << "client -> server: -2";
    ModbusClient->SetTextValue(holding20, "-2");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ("18446744073709551614", ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(0xFFFF, Slave->Holding[20]);
    EXPECT_EQ(0xFFFF, Slave->Holding[21]);
    EXPECT_EQ(0xFFFF, Slave->Holding[22]);
    EXPECT_EQ(0xFFFE, Slave->Holding[23]);
}


TEST_F(TModbusClientTest, S32)
{
    PModbusRegister holding20 (new TModbusRegister(1, TModbusRegister::HOLDING_REGISTER, 20, S32));
    PModbusRegister input30(new TModbusRegister(1, TModbusRegister::INPUT_REGISTER, 30, S32));
    ModbusClient->AddRegister(holding20);
    ModbusClient->AddRegister(input30);

    Note() << "server -> client: 10, 20";
    Slave->Holding[20] = 0x00AA;
    Slave->Holding[21] = 0x00BB;
    Slave->Input[30] = 0xFFFF;
    Slave->Input[31] = 0xFFFF;
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(to_string(-1), ModbusClient->GetTextValue(input30));

    FakeSerial->Flush();
    Note() << "client -> server: 10";
    ModbusClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(10), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(0, Slave->Holding[20]);
    EXPECT_EQ(10, Slave->Holding[21]);

    FakeSerial->Flush();
    Note() << "client -> server: -2";
    ModbusClient->SetTextValue(holding20, "-2");
    EXPECT_EQ(to_string(-2), ModbusClient->GetTextValue(holding20));
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(-2), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(0xFFFF, Slave->Holding[20]);
    EXPECT_EQ(0xFFFE, Slave->Holding[21]);

}

TEST_F(TModbusClientTest, U32)
{
    PModbusRegister holding20 (new TModbusRegister(1, TModbusRegister::HOLDING_REGISTER, 20, U32));
    PModbusRegister input30(new TModbusRegister(1, TModbusRegister::INPUT_REGISTER, 30, U32));
    ModbusClient->AddRegister(holding20);
    ModbusClient->AddRegister(input30);

    Note() << "server -> client: 10, 20";
    Slave->Holding[20] = 0x00AA;
    Slave->Holding[21] = 0x00BB;
    Slave->Input[30] = 0xFFFF;
    Slave->Input[31] = 0xFFFF;
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(to_string(0xFFFFFFFF), ModbusClient->GetTextValue(input30));

    FakeSerial->Flush();
    Note() << "client -> server: 10";
    ModbusClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(10), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(0, Slave->Holding[20]);
    EXPECT_EQ(10, Slave->Holding[21]);

    FakeSerial->Flush();
    Note() << "client -> server: -1 (overflow)";
    ModbusClient->SetTextValue(holding20, "-1");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(0xFFFFFFFF), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(0xFFFF, Slave->Holding[20]);
    EXPECT_EQ(0xFFFF, Slave->Holding[21]);

    FakeSerial->Flush();
    Slave->Holding[22] = 123;
    Slave->Holding[23] = 123;
    Note() << "client -> server: 4294967296 (overflow)";
    ModbusClient->SetTextValue(holding20, "4294967296");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(0), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(0, Slave->Holding[20]);
    EXPECT_EQ(0, Slave->Holding[21]);

    //boundaries check
    EXPECT_EQ(123, Slave->Holding[22]);
    EXPECT_EQ(123, Slave->Holding[23]);
}


TEST_F(TModbusClientTest, Float32)
{
	// create scaled register
    PModbusRegister holding24 (new TModbusRegister(1, TModbusRegister::HOLDING_REGISTER, 24, Float, 100));
    ModbusClient->AddRegister(holding24);

    PModbusRegister holding20 (new TModbusRegister(1, TModbusRegister::HOLDING_REGISTER, 20, Float));
    PModbusRegister input30(new TModbusRegister(1, TModbusRegister::INPUT_REGISTER, 30, Float));
    ModbusClient->AddRegister(holding20);
    ModbusClient->AddRegister(input30);

    Note() << "server -> client: 0x45d2 0x0000, 0x449d 0x8000";
    Slave->Holding[20] = 0x45d2;
    Slave->Holding[21] = 0x0000;
    Slave->Input[30] = 0x449d;
    Slave->Input[31] = 0x8000;
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(6720.0), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(to_string(1260.0), ModbusClient->GetTextValue(input30));

    FakeSerial->Flush();
    Note() << "client -> server: 10";
    ModbusClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(10.), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(0x4120, Slave->Holding[20]);
    EXPECT_EQ(0x0000, Slave->Holding[21]);

    FakeSerial->Flush();
    Note() << "client -> server: -0.00123";
    ModbusClient->SetTextValue(holding20, "-0.00123");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(-0.00123), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(0xbaa1, Slave->Holding[20]);
    EXPECT_EQ(0x37f4, Slave->Holding[21]);

    FakeSerial->Flush();
    Note() << "client -> server: -0.123 (scaled)";
    ModbusClient->SetTextValue(holding24, "-0.123");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(-0.123), ModbusClient->GetTextValue(holding24));
    EXPECT_EQ(0xbaa1, Slave->Holding[24]);
    EXPECT_EQ(0x37f4, Slave->Holding[25]);

    FakeSerial->Flush();
    Note() << "server -> client: 0x449d 0x8000 (scaled)";
    Slave->Holding[24] = 0x449d;
    Slave->Holding[25] = 0x8000;
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(126000.0), ModbusClient->GetTextValue(holding24));
}

TEST_F(TModbusClientTest, Double64)
{
	// create scaled register
    PModbusRegister holding24 (new TModbusRegister(1, TModbusRegister::HOLDING_REGISTER, 24, Double, 100));
    ModbusClient->AddRegister(holding24);

    PModbusRegister holding20 (new TModbusRegister(1, TModbusRegister::HOLDING_REGISTER, 20, Double));
    PModbusRegister input30(new TModbusRegister(1, TModbusRegister::INPUT_REGISTER, 30, Double));
    ModbusClient->AddRegister(holding20);
    ModbusClient->AddRegister(input30);

    Note() << "server -> client: 40ba401f7ced9168 , 4093b148b4395810";
    Slave->Holding[20] = 0x40ba;
    Slave->Holding[21] = 0x401f;
    Slave->Holding[22] = 0x7ced;
    Slave->Holding[23] = 0x9168;

    Slave->Input[30] = 0x4093;
    Slave->Input[31] = 0xb148;
    Slave->Input[32] = 0xb439;
    Slave->Input[33] = 0x5810;

    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(6720.123), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(to_string(1260.321), ModbusClient->GetTextValue(input30));

    FakeSerial->Flush();
    Note() << "client -> server: 10";
    ModbusClient->SetTextValue(holding20, "10.9999");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(10.9999), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(0x4025, Slave->Holding[20]);
    EXPECT_EQ(0xfff2, Slave->Holding[21]);
    EXPECT_EQ(0xe48e, Slave->Holding[22]);
    EXPECT_EQ(0x8a72, Slave->Holding[23]);

    FakeSerial->Flush();
    Note() << "client -> server: -0.00123";
    ModbusClient->SetTextValue(holding20, "-0.00123");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(-0.00123), ModbusClient->GetTextValue(holding20));
    EXPECT_EQ(0xbf54, Slave->Holding[20]);
    EXPECT_EQ(0x26fe, Slave->Holding[21]);
    EXPECT_EQ(0x718a, Slave->Holding[22]);
    EXPECT_EQ(0x86d7, Slave->Holding[23]);


    FakeSerial->Flush();
    Note() << "client -> server: -0.123 (scaled)";
    ModbusClient->SetTextValue(holding24, "-0.123");
    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(-0.123), ModbusClient->GetTextValue(holding24));
    EXPECT_EQ(0xbf54, Slave->Holding[24]);
    EXPECT_EQ(0x26fe, Slave->Holding[25]);
    EXPECT_EQ(0x718a, Slave->Holding[26]);
    EXPECT_EQ(0x86d7, Slave->Holding[27]);

    FakeSerial->Flush();
    Note() << "server -> client: 4093b00000000000 (scaled)";
    Slave->Holding[24] = 0x4093;
    Slave->Holding[25] = 0xb000;
    Slave->Holding[26] = 0x0000;
    Slave->Holding[27] = 0x0000;

    Note() << "Cycle()";
    ModbusClient->Cycle();
    EXPECT_EQ(to_string(126000.0), ModbusClient->GetTextValue(holding24));
}

TEST_F(TModbusClientTest, Errors)
{
    PModbusRegister holding20(new TModbusRegister(1, TModbusRegister::HOLDING_REGISTER, 20));
    Slave->Holding.BlacklistRead(20, true);
    Slave->Holding.BlacklistWrite(20, true);
    ModbusClient->AddRegister(holding20);

    for (int i = 0; i < 3; i++) {
        FakeSerial->Flush();
        Note() << "Cycle() [read, rw blacklisted]";
        ModbusClient->Cycle();
    }

    FakeSerial->Flush();
    ModbusClient->SetTextValue(holding20, "42");
    Note() << "Cycle() [write, rw blacklisted]";
    ModbusClient->Cycle();

    FakeSerial->Flush();
    Slave->Holding.BlacklistWrite(20, false);
    ModbusClient->SetTextValue(holding20, "42");
    Note() << "Cycle() [write, r blacklisted]";
    ModbusClient->Cycle();

    FakeSerial->Flush();
    Slave->Holding.BlacklistWrite(20, true);
    ModbusClient->SetTextValue(holding20, "43");
    Note() << "Cycle() [write, rw blacklisted]";
    ModbusClient->Cycle();

    FakeSerial->Flush();
    Slave->Holding.BlacklistRead(20, false);
    Note() << "Cycle() [write, w blacklisted]";
    ModbusClient->Cycle();

    FakeSerial->Flush();
    Slave->Holding.BlacklistWrite(20, false);
    ModbusClient->SetTextValue(holding20, "42");
    Note() << "Cycle() [write, nothing blacklisted]";
    ModbusClient->Cycle();
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

class TModbusDeviceTest: public TModbusTestBase
{
protected:
    void SetUp();
    void FilterConfig(const std::string& device_name);
    PHandlerConfig Config;
    PFakeMQTTClient MQTTClient;
};

void TModbusDeviceTest::SetUp()
{
    TModbusTestBase::SetUp();
    TConfigParser parser(GetDataFilePath("../config-test.json"), false);
    Config = parser.Parse();
    // NOTE: only one port is currently supported
    Config->PortConfigs[0]->ConnSettings.Device = FakeSerial->GetPrimaryPtsName();
    MQTTClient = PFakeMQTTClient(new TFakeMQTTClient("modbus-test", *this));
}

void TModbusDeviceTest::FilterConfig(const std::string& device_name)
{
    for (auto port_config: Config->PortConfigs) {
		cout << "port devices: " << port_config->DeviceConfigs.size() << endl;
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
    PModbusSlave slave = ModbusServer->SetSlave(
        Config->PortConfigs[0]->DeviceConfigs[0]->SlaveId,
        TModbusRange(
            TRegisterRange(),
            TRegisterRange(),
            TRegisterRange(4, 19),
            TRegisterRange()));
    ModbusServer->Start();

    PMQTTModbusObserver modbus_observer(new TMQTTModbusObserver(MQTTClient, Config));
    modbus_observer->SetUp();

    Note() << "ModbusLoopOnce()";
    modbus_observer->ModbusLoopOnce();

    MQTTClient->DoPublish(true, 0, "/devices/ddl24/controls/RGB/on", "10;20;30");

    FakeSerial->Flush();
    Note() << "ModbusLoopOnce()";
    modbus_observer->ModbusLoopOnce();
    ASSERT_EQ(10, slave->Holding[4]);
    ASSERT_EQ(20, slave->Holding[5]);
    ASSERT_EQ(30, slave->Holding[6]);

    FakeSerial->Flush();
    slave->Holding[4] = 32;
    slave->Holding[5] = 64;
    slave->Holding[6] = 128;
    Note() << "ModbusLoopOnce() after slave update";
    modbus_observer->ModbusLoopOnce();
}

TEST_F(TModbusDeviceTest, OnValue)
{
    FilterConfig("OnValueTest");
    PModbusSlave slave = ModbusServer->SetSlave(
        Config->PortConfigs[0]->DeviceConfigs[0]->SlaveId,
        TModbusRange(
            TRegisterRange(),
            TRegisterRange(),
            TRegisterRange(0, 1),
            TRegisterRange()));
    ModbusServer->Start();

    PMQTTModbusObserver modbus_observer(new TMQTTModbusObserver(MQTTClient, Config));
    modbus_observer->SetUp();

    slave->Holding[0] = 0;
    Note() << "ModbusLoopOnce()";
    modbus_observer->ModbusLoopOnce();

    MQTTClient->DoPublish(true, 0, "/devices/OnValueTest/controls/Relay 1/on", "1");

    Note() << "ModbusLoopOnce()";
    modbus_observer->ModbusLoopOnce();
    ASSERT_EQ(500, slave->Holding[0]);

    slave->Holding[0] = 0;
    Note() << "ModbusLoopOnce() after slave update";
    modbus_observer->ModbusLoopOnce();

    slave->Holding[0] = 500;
    Note() << "ModbusLoopOnce() after second slave update";
    modbus_observer->ModbusLoopOnce();
}

TEST_F(TModbusDeviceTest, Errors)
{
    FilterConfig("DDL24");
    PModbusSlave slave = ModbusServer->SetSlave(
        Config->PortConfigs[0]->DeviceConfigs[0]->SlaveId,
        TModbusRange(
            TRegisterRange(),
            TRegisterRange(),
            TRegisterRange(4, 19),
            TRegisterRange()));
    ModbusServer->Start();

    PMQTTModbusObserver modbus_observer(new TMQTTModbusObserver(MQTTClient, Config));
    modbus_observer->SetUp();

    slave->Holding.BlacklistRead(4, true);
    slave->Holding.BlacklistWrite(4, true);
    slave->Holding.BlacklistRead(7, true);
    slave->Holding.BlacklistWrite(7, true);

    Note() << "ModbusLoopOnce() [read, rw blacklisted]";
    modbus_observer->ModbusLoopOnce();

    MQTTClient->DoPublish(true, 0, "/devices/ddl24/controls/RGB/on", "10;20;30");
    MQTTClient->DoPublish(true, 0, "/devices/ddl24/controls/White/on", "42");

    Note() << "ModbusLoopOnce() [write, rw blacklisted]";
    modbus_observer->ModbusLoopOnce();

    slave->Holding.BlacklistRead(4, false);
    slave->Holding.BlacklistWrite(4, false);
    slave->Holding.BlacklistRead(7, false);
    slave->Holding.BlacklistWrite(7, false);

    Note() << "ModbusLoopOnce() [read, nothing blacklisted]";
    modbus_observer->ModbusLoopOnce();

    MQTTClient->DoPublish(true, 0, "/devices/ddl24/controls/RGB/on", "10;20;30");
    MQTTClient->DoPublish(true, 0, "/devices/ddl24/controls/White/on", "42");

    Note() << "ModbusLoopOnce() [write, nothing blacklisted]";
    modbus_observer->ModbusLoopOnce();

    Note() << "ModbusLoopOnce() [read, nothing blacklisted] (2)";
    modbus_observer->ModbusLoopOnce();
}

// TBD: the code must check mosquitto return values
