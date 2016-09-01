#include <map>
#include <memory>
#include <algorithm>
#include <cassert>
#include <gtest/gtest.h>

#include "testlog.h"
#include "fake_mqtt.h"
#include "serial_config.h"
#include "serial_observer.h"
#include "modbus_server.h"
#include "serial_device.h"
#include "modbus_device.h"
#include "pty_based_fake_serial.h"

using namespace std;

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
        TSerialPortSettings(FakeSerial->GetSecondaryPtsName(),
                            9600, 'N', 8, 1,
                            std::chrono::milliseconds(10000)));
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
    PRegister RegCoil(int addr, RegisterFormat fmt = U8) {
        return TRegister::Intern(
            TSlaveEntry::Intern("modbus", 1),
            TModbusDevice::REG_COIL, addr, fmt, 1, true, false, "coil");
    }
    PRegister RegDiscrete(int addr, RegisterFormat fmt = U8) {
        return TRegister::Intern(
            TSlaveEntry::Intern("modbus", 1),
            TModbusDevice::REG_DISCRETE, addr, fmt, 1, true, true, "discrete");
    }
    PRegister RegHolding(int addr, RegisterFormat fmt = U16, double scale = 1) {
        return TRegister::Intern(
            TSlaveEntry::Intern("modbus", 1),
            TModbusDevice::REG_HOLDING, addr, fmt, scale, true, false, "holding");
    }
    PRegister RegInput(int addr, RegisterFormat fmt = U16, double scale = 1) {
        return TRegister::Intern(
            TSlaveEntry::Intern("modbus", 1),
            TModbusDevice::REG_INPUT, addr, fmt, scale, true, true, "input");
    }
    PSerialPort ClientSerial;
    PSerialClient SerialClient;
    PModbusSlave Slave;
};

void TModbusClientTest::SetUp()
{
    TModbusTestBase::SetUp();

    Slave = ModbusServer->SetSlave(
        1, TModbusRange(
            TServerRegisterRange(0, 10),
            TServerRegisterRange(10, 20),
            TServerRegisterRange(20, 30),
            TServerRegisterRange(30, 40)));
    ModbusServer->Start();

    ClientSerial = std::make_shared<TSerialPort>(
        TSerialPortSettings(FakeSerial->GetPrimaryPtsName(), 9600, 'N', 8, 1));
    SerialClient = std::make_shared<TSerialClient>(ClientSerial);
#if 0
    SerialClient->SetModbusDebug(true);
#endif
    SerialClient->AddDevice(std::make_shared<TDeviceConfig>("modbus_sample", 1, "modbus"));
    SerialClient->SetReadCallback([this](PRegister reg, bool changed) {
            Emit() << "Modbus Callback: " << reg->ToString() << " becomes " <<
                SerialClient->GetTextValue(reg) << (changed ? "" : " [unchanged]");
        });
    SerialClient->SetErrorCallback(
        [this](PRegister reg, TRegisterHandler::TErrorState errorState) {
            const char* what;
            switch (errorState) {
            case TRegisterHandler::WriteError:
                what = "write error";
                break;
            case TRegisterHandler::ReadError:
                what = "read error";
                break;
            case TRegisterHandler::ReadWriteError:
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
    SerialClient.reset();
    TModbusTestBase::TearDown();
}

TEST_F(TModbusClientTest, Poll)
{
    PRegister coil0 = RegCoil(0);
    PRegister coil1 = RegCoil(1);
    PRegister discrete10 = RegDiscrete(10);
    PRegister holding22 = RegHolding(22);
    PRegister input33 = RegInput(33);

    SerialClient->AddRegister(coil0);
    SerialClient->AddRegister(coil1);
    SerialClient->AddRegister(discrete10);
    SerialClient->AddRegister(holding22);
    SerialClient->AddRegister(input33);

    Note() << "Cycle()";
    SerialClient->Cycle();

    Slave->Coils[1] = 1;
    Slave->Discrete[10] = 1;
    Slave->Holding[22] = 4242;
    Slave->Input[33] = 42000;

    FakeSerial->Flush();
    Note() << "Cycle()";
    SerialClient->Cycle();

    EXPECT_EQ(to_string(0), SerialClient->GetTextValue(coil0));
    EXPECT_EQ(to_string(1), SerialClient->GetTextValue(coil1));
    EXPECT_EQ(to_string(1), SerialClient->GetTextValue(discrete10));
    EXPECT_EQ(to_string(4242), SerialClient->GetTextValue(holding22));
    EXPECT_EQ(to_string(42000), SerialClient->GetTextValue(input33));
}

TEST_F(TModbusClientTest, Write)
{
    PRegister coil1 = RegCoil(1);
    PRegister holding20 = RegHolding(20);
    SerialClient->AddRegister(coil1);
    SerialClient->AddRegister(holding20);

    Note() << "Cycle()";
    SerialClient->Cycle();

    SerialClient->SetTextValue(coil1, "1");
    SerialClient->SetTextValue(holding20, "4242");

    for (int i = 0; i < 3; ++i) {
        FakeSerial->Flush();
        Note() << "Cycle()";
        SerialClient->Cycle();

        EXPECT_EQ(to_string(1), SerialClient->GetTextValue(coil1));
        EXPECT_EQ(1, Slave->Coils[1]);
        EXPECT_EQ(to_string(4242), SerialClient->GetTextValue(holding20));
        EXPECT_EQ(4242, Slave->Holding[20]);
    }
}

TEST_F(TModbusClientTest, S8)
{
    PRegister holding20 = RegHolding(20, S8);
    PRegister input30 = RegInput(30, S8);
    SerialClient->AddRegister(holding20);
    SerialClient->AddRegister(input30);

    Note() << "server -> client: 10, 20";
    Slave->Holding[20] = 10;
    Slave->Input[30] = 20;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(to_string(20), SerialClient->GetTextValue(input30));

    FakeSerial->Flush();
    Note() << "server -> client: -2, -3";
    Slave->Holding[20] = 254;
    Slave->Input[30] = 253;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(to_string(-3), SerialClient->GetTextValue(input30));

    FakeSerial->Flush();
    Note() << "client -> server: 10";
    SerialClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(10, Slave->Holding[20]);

    FakeSerial->Flush();
    Note() << "client -> server: -2";
    SerialClient->SetTextValue(holding20, "-2");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(254, Slave->Holding[20]);
}

TEST_F(TModbusClientTest, Char8)
{
    PRegister holding20 = RegHolding(20, Char8);
    PRegister input30 = RegInput(30, Char8);
    SerialClient->AddRegister(holding20);
    SerialClient->AddRegister(input30);

    Note() << "server -> client: 65, 66";
    Slave->Holding[20] = 65;
    Slave->Input[30] = 66;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("A", SerialClient->GetTextValue(holding20));
    EXPECT_EQ("B", SerialClient->GetTextValue(input30));

    FakeSerial->Flush();
    Note() << "client -> server: '!'";
    SerialClient->SetTextValue(holding20, "!");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("!", SerialClient->GetTextValue(holding20));
    EXPECT_EQ(33, Slave->Holding[20]);
    FakeSerial->Flush();
}

TEST_F(TModbusClientTest, S64)
{
    PRegister holding20 = RegHolding(20, S64);
    PRegister input30 = RegInput(30, S64);
    SerialClient->AddRegister(holding20);
    SerialClient->AddRegister(input30);

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
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB00CC00DD), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(to_string(-1), SerialClient->GetTextValue(input30));

    FakeSerial->Flush();
    Note() << "client -> server: 10";
    SerialClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0, Slave->Holding[20]);
    EXPECT_EQ(0, Slave->Holding[21]);
    EXPECT_EQ(0, Slave->Holding[22]);
    EXPECT_EQ(10, Slave->Holding[23]);

    FakeSerial->Flush();
    Note() << "client -> server: -2";
    SerialClient->SetTextValue(holding20, "-2");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0xFFFF, Slave->Holding[20]);
    EXPECT_EQ(0xFFFF, Slave->Holding[21]);
    EXPECT_EQ(0xFFFF, Slave->Holding[22]);
    EXPECT_EQ(0xFFFE, Slave->Holding[23]);
}

TEST_F(TModbusClientTest, U64)
{
    PRegister holding20 = RegHolding(20, U64);
    PRegister input30 = RegInput(30, U64);
    SerialClient->AddRegister(holding20);
    SerialClient->AddRegister(input30);

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
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB00CC00DD), SerialClient->GetTextValue(holding20));
    EXPECT_EQ("18446744073709551615", SerialClient->GetTextValue(input30));

    FakeSerial->Flush();
    Note() << "client -> server: 10";
    SerialClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0, Slave->Holding[20]);
    EXPECT_EQ(0, Slave->Holding[21]);
    EXPECT_EQ(0, Slave->Holding[22]);
    EXPECT_EQ(10, Slave->Holding[23]);

    FakeSerial->Flush();
    Note() << "client -> server: -2";
    SerialClient->SetTextValue(holding20, "-2");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("18446744073709551614", SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0xFFFF, Slave->Holding[20]);
    EXPECT_EQ(0xFFFF, Slave->Holding[21]);
    EXPECT_EQ(0xFFFF, Slave->Holding[22]);
    EXPECT_EQ(0xFFFE, Slave->Holding[23]);
}

TEST_F(TModbusClientTest, S32)
{
    PRegister holding20 = RegHolding(20, S32);
    PRegister input30 = RegInput(30, S32);
    SerialClient->AddRegister(holding20);
    SerialClient->AddRegister(input30);

    // create scaled register
    PRegister holding24 = RegHolding(24, S32, 0.001);
    SerialClient->AddRegister(holding24);

    Note() << "server -> client: 10, 20";
    Slave->Holding[20] = 0x00AA;
    Slave->Holding[21] = 0x00BB;
    Slave->Input[30] = 0xFFFF;
    Slave->Input[31] = 0xFFFF;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(to_string(-1), SerialClient->GetTextValue(input30));

    FakeSerial->Flush();
    Note() << "client -> server: 10";
    SerialClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0, Slave->Holding[20]);
    EXPECT_EQ(10, Slave->Holding[21]);

    FakeSerial->Flush();
    Note() << "client -> server: -2";
    SerialClient->SetTextValue(holding20, "-2");
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(holding20));
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0xFFFF, Slave->Holding[20]);
    EXPECT_EQ(0xFFFE, Slave->Holding[21]);

    Note() << "client -> server: -0.123 (scaled)";
    SerialClient->SetTextValue(holding24, "-0.123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", SerialClient->GetTextValue(holding24));
    EXPECT_EQ(0xffff, Slave->Holding[24]);
    EXPECT_EQ(0xff85, Slave->Holding[25]);

    Note() << "server -> client: 0xffff 0xff85 (scaled)";
    Slave->Holding[24] = 0xffff;
    Slave->Holding[25] = 0xff85;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", SerialClient->GetTextValue(holding24));
}

TEST_F(TModbusClientTest, U32)
{
    PRegister holding20 = RegHolding(20, U32);
    PRegister input30 = RegInput(30, U32);
    SerialClient->AddRegister(holding20);
    SerialClient->AddRegister(input30);

    Note() << "server -> client: 10, 20";
    Slave->Holding[20] = 0x00AA;
    Slave->Holding[21] = 0x00BB;
    Slave->Input[30] = 0xFFFF;
    Slave->Input[31] = 0xFFFF;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(to_string(0xFFFFFFFF), SerialClient->GetTextValue(input30));

    FakeSerial->Flush();
    Note() << "client -> server: 10";
    SerialClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0, Slave->Holding[20]);
    EXPECT_EQ(10, Slave->Holding[21]);

    FakeSerial->Flush();
    Note() << "client -> server: -1 (overflow)";
    SerialClient->SetTextValue(holding20, "-1");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0xFFFFFFFF), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0xFFFF, Slave->Holding[20]);
    EXPECT_EQ(0xFFFF, Slave->Holding[21]);

    FakeSerial->Flush();
    Slave->Holding[22] = 123;
    Slave->Holding[23] = 123;
    Note() << "client -> server: 4294967296 (overflow)";
    SerialClient->SetTextValue(holding20, "4294967296");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0, Slave->Holding[20]);
    EXPECT_EQ(0, Slave->Holding[21]);

    //boundaries check
    EXPECT_EQ(123, Slave->Holding[22]);
    EXPECT_EQ(123, Slave->Holding[23]);
}

TEST_F(TModbusClientTest, Float32)
{
	// create scaled register
    PRegister holding24 = RegHolding(24, Float, 100);
    SerialClient->AddRegister(holding24);

    PRegister holding20 = RegHolding(20, Float);
    PRegister input30 = RegInput(30, Float);
    SerialClient->AddRegister(holding20);
    SerialClient->AddRegister(input30);

    Note() << "server -> client: 0x45d2 0x0000, 0x449d 0x8000";
    Slave->Holding[20] = 0x45d2;
    Slave->Holding[21] = 0x0000;
    Slave->Input[30] = 0x449d;
    Slave->Input[31] = 0x8000;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("6720", SerialClient->GetTextValue(holding20));
    EXPECT_EQ("1260", SerialClient->GetTextValue(input30));

    FakeSerial->Flush();
    Note() << "client -> server: 10";
    SerialClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("10", SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0x4120, Slave->Holding[20]);
    EXPECT_EQ(0x0000, Slave->Holding[21]);

    FakeSerial->Flush();
    Note() << "client -> server: -0.00123";
    SerialClient->SetTextValue(holding20, "-0.00123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.00123", SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0xbaa1, Slave->Holding[20]);
    EXPECT_EQ(0x37f4, Slave->Holding[21]);

    FakeSerial->Flush();
    Note() << "client -> server: -0.123 (scaled)";
    SerialClient->SetTextValue(holding24, "-0.123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", SerialClient->GetTextValue(holding24));
    EXPECT_EQ(0xbaa1, Slave->Holding[24]);
    EXPECT_EQ(0x37f4, Slave->Holding[25]);

    FakeSerial->Flush();
    Note() << "server -> client: 0x449d 0x8000 (scaled)";
    Slave->Holding[24] = 0x449d;
    Slave->Holding[25] = 0x8000;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("126000", SerialClient->GetTextValue(holding24));
}

TEST_F(TModbusClientTest, Double64)
{
	// create scaled register
    PRegister holding24 = RegHolding(24, Double, 100);
    SerialClient->AddRegister(holding24);

    PRegister holding20 = RegHolding(20, Double);
    PRegister input30 = RegInput(30, Double);
    SerialClient->AddRegister(holding20);
    SerialClient->AddRegister(input30);

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
    SerialClient->Cycle();
    EXPECT_EQ("6720.123", SerialClient->GetTextValue(holding20));
    EXPECT_EQ("1260.321", SerialClient->GetTextValue(input30));

    FakeSerial->Flush();
    Note() << "client -> server: 10";
    SerialClient->SetTextValue(holding20, "10.9999");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("10.9999", SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0x4025, Slave->Holding[20]);
    EXPECT_EQ(0xfff2, Slave->Holding[21]);
    EXPECT_EQ(0xe48e, Slave->Holding[22]);
    EXPECT_EQ(0x8a72, Slave->Holding[23]);

    FakeSerial->Flush();
    Note() << "client -> server: -0.00123";
    SerialClient->SetTextValue(holding20, "-0.00123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.00123", SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0xbf54, Slave->Holding[20]);
    EXPECT_EQ(0x26fe, Slave->Holding[21]);
    EXPECT_EQ(0x718a, Slave->Holding[22]);
    EXPECT_EQ(0x86d7, Slave->Holding[23]);

    FakeSerial->Flush();
    Note() << "client -> server: -0.123 (scaled)";
    SerialClient->SetTextValue(holding24, "-0.123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", SerialClient->GetTextValue(holding24));
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
    SerialClient->Cycle();
    EXPECT_EQ("126000", SerialClient->GetTextValue(holding24));
}

TEST_F(TModbusClientTest, Errors)
{
    PRegister holding20 = RegHolding(20);
    Slave->Holding.BlacklistRead(20, true);
    Slave->Holding.BlacklistWrite(20, true);
    SerialClient->AddRegister(holding20);

    for (int i = 0; i < 3; i++) {
        FakeSerial->Flush();
        Note() << "Cycle() [read, rw blacklisted]";
        SerialClient->Cycle();
    }

    FakeSerial->Flush();
    SerialClient->SetTextValue(holding20, "42");
    Note() << "Cycle() [write, rw blacklisted]";
    SerialClient->Cycle();

    FakeSerial->Flush();
    Slave->Holding.BlacklistWrite(20, false);
    SerialClient->SetTextValue(holding20, "42");
    Note() << "Cycle() [write, r blacklisted]";
    SerialClient->Cycle();

    FakeSerial->Flush();
    Slave->Holding.BlacklistWrite(20, true);
    SerialClient->SetTextValue(holding20, "43");
    Note() << "Cycle() [write, rw blacklisted]";
    SerialClient->Cycle();

    FakeSerial->Flush();
    Slave->Holding.BlacklistRead(20, false);
    Note() << "Cycle() [write, w blacklisted]";
    SerialClient->Cycle();

    FakeSerial->Flush();
    Slave->Holding.BlacklistWrite(20, false);
    SerialClient->SetTextValue(holding20, "42");
    Note() << "Cycle() [write, nothing blacklisted]";
    SerialClient->Cycle();


    holding20->HasErrorValue = true;
    holding20->ErrorValue = 42;
    Note() << "Cycle() [read, set error value for register]";
    SerialClient->Cycle();
    SerialClient->GetTextValue(holding20);

    SerialClient->Cycle();
}

class TConfigParserTest: public TLoggedFixture {};

TEST_F(TConfigParserTest, Parse)
{
    TConfigTemplateParser device_parser(GetDataFilePath("../wb-mqtt-serial-templates/"), false);
    TConfigParser parser(GetDataFilePath("../config.json"), false,
                         TSerialDeviceFactory::GetRegisterTypes, device_parser.Parse());
    PHandlerConfig config = parser.Parse();
    Emit() << "Debug: " << config->Debug;
    Emit() << "Ports:";
    for (auto port_config: config->PortConfigs) {
        TTestLogIndent indent(*this);
        ASSERT_EQ(config->Debug, port_config->Debug);
        Emit() << "------";
        Emit() << "ConnSettings: " << port_config->ConnSettings;
        Emit() << "PollInterval: " << port_config->PollInterval.count();
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
            if (!device_config->DeviceChannels.empty()) {
                Emit() << "DeviceChannels:";
                for (auto modbus_channel: device_config->DeviceChannels) {
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
                        if (reg->PollInterval.count())
                            s << " (poll_interval=" << reg->PollInterval.count() << ")";
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
                    Emit() << "Address: " << setup_item->Reg->Address;
                    Emit() << "Value: " << setup_item->Value;
                }
            }
        }
    }

}

TEST_F(TConfigParserTest, ForceDebug)
{
    TConfigParser parser(GetDataFilePath("../config-test.json"), true,
                         TSerialDeviceFactory::GetRegisterTypes);
    PHandlerConfig config = parser.Parse();
    ASSERT_TRUE(config->Debug);
}

class TModbusDeviceTest: public TModbusTestBase
{
protected:
    void SetUp();
    void FilterConfig(const std::string& device_name);
    void VerifyDDL24(); // used with two different configs
    PHandlerConfig Config;
    PFakeMQTTClient MQTTClient;
};

void TModbusDeviceTest::SetUp()
{
    TModbusTestBase::SetUp();
    TConfigParser parser(GetDataFilePath("../config-test.json"), false,
                         TSerialDeviceFactory::GetRegisterTypes);
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

void TModbusDeviceTest::VerifyDDL24()
{
    PModbusSlave slave = ModbusServer->SetSlave(
        Config->PortConfigs[0]->DeviceConfigs[0]->SlaveId,
        TModbusRange(
            TServerRegisterRange(),
            TServerRegisterRange(),
            TServerRegisterRange(4, 19),
            TServerRegisterRange()));
    ModbusServer->Start();

    PMQTTSerialObserver observer(new TMQTTSerialObserver(MQTTClient, Config));
    observer->SetUp();

    Note() << "LoopOnce()";
    observer->LoopOnce();

    MQTTClient->DoPublish(true, 0, "/devices/ddl24/controls/RGB/on", "10;20;30");

    FakeSerial->Flush();
    Note() << "LoopOnce()";
    observer->LoopOnce();
    ASSERT_EQ(10, slave->Holding[4]);
    ASSERT_EQ(20, slave->Holding[5]);
    ASSERT_EQ(30, slave->Holding[6]);

    FakeSerial->Flush();
    slave->Holding[4] = 32;
    slave->Holding[5] = 64;
    slave->Holding[6] = 128;
    Note() << "LoopOnce() after slave update";

    observer->LoopOnce();
}

TEST_F(TModbusDeviceTest, DDL24)
{
    FilterConfig("DDL24");
    VerifyDDL24();
}

TEST_F(TModbusDeviceTest, DDL24_Holes)
{
    FilterConfig("DDL24");
    Config->PortConfigs[0]->DeviceConfigs[0]->MaxRegHole = 10;
    Config->PortConfigs[0]->DeviceConfigs[0]->MaxBitHole = 80;
    VerifyDDL24();
}

TEST_F(TModbusDeviceTest, OnValue)
{
    FilterConfig("OnValueTest");
    PModbusSlave slave = ModbusServer->SetSlave(
        Config->PortConfigs[0]->DeviceConfigs[0]->SlaveId,
        TModbusRange(
            TServerRegisterRange(),
            TServerRegisterRange(),
            TServerRegisterRange(0, 1),
            TServerRegisterRange()));
    ModbusServer->Start();

    PMQTTSerialObserver observer(new TMQTTSerialObserver(MQTTClient, Config));
    observer->SetUp();

    slave->Holding[0] = 0;
    Note() << "LoopOnce()";
    observer->LoopOnce();

    MQTTClient->DoPublish(true, 0, "/devices/OnValueTest/controls/Relay 1/on", "1");

    Note() << "LoopOnce()";
    observer->LoopOnce();
    ASSERT_EQ(500, slave->Holding[0]);

    slave->Holding[0] = 0;
    Note() << "LoopOnce() after slave update";
    observer->LoopOnce();

    slave->Holding[0] = 500;
    Note() << "LoopOnce() after second slave update";
    observer->LoopOnce();
}

TEST_F(TModbusDeviceTest, Errors)
{
    FilterConfig("DDL24");
    PModbusSlave slave = ModbusServer->SetSlave(
        Config->PortConfigs[0]->DeviceConfigs[0]->SlaveId,
        TModbusRange(
            TServerRegisterRange(),
            TServerRegisterRange(),
            TServerRegisterRange(4, 19),
            TServerRegisterRange()));
    ModbusServer->Start();

    PMQTTSerialObserver observer(new TMQTTSerialObserver(MQTTClient, Config));
    observer->SetUp();

    slave->Holding.BlacklistRead(4, true);
    slave->Holding.BlacklistWrite(4, true);
    slave->Holding.BlacklistRead(7, true);
    slave->Holding.BlacklistWrite(7, true);

    Note() << "LoopOnce() [read, rw blacklisted]";
    observer->LoopOnce();

    MQTTClient->DoPublish(true, 0, "/devices/ddl24/controls/RGB/on", "10;20;30");
    MQTTClient->DoPublish(true, 0, "/devices/ddl24/controls/White/on", "42");

    Note() << "LoopOnce() [write, rw blacklisted]";
    observer->LoopOnce();

    slave->Holding.BlacklistRead(4, false);
    slave->Holding.BlacklistWrite(4, false);
    slave->Holding.BlacklistRead(7, false);
    slave->Holding.BlacklistWrite(7, false);

    Note() << "LoopOnce() [read, nothing blacklisted]";
    observer->LoopOnce();

    MQTTClient->DoPublish(true, 0, "/devices/ddl24/controls/RGB/on", "10;20;30");
    MQTTClient->DoPublish(true, 0, "/devices/ddl24/controls/White/on", "42");

    Note() << "LoopOnce() [write, nothing blacklisted]";
    observer->LoopOnce();

    Note() << "LoopOnce() [read, nothing blacklisted] (2)";
    observer->LoopOnce();
}

// TBD: the code must check mosquitto return values
