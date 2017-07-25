#include <string>
#include <map>
#include <memory>
#include <algorithm>
#include <cassert>
#include <gtest/gtest.h>

#include "testlog.h"
#include "serial_observer.h"
#include "fake_mqtt.h"
#include "fake_serial_port.h"
#include "fake_serial_device.h"

using namespace std;


class TSerialClientTest: public TLoggedFixture
{
protected:
    void SetUp();
    void TearDown();
    PRegister RegCoil(int addr, RegisterFormat fmt = U8) {
        return TRegister::Intern(
            Device, TRegisterConfig::Create(
            TFakeSerialDevice::REG_COIL, addr, fmt, 1, 0, true, false, "coil"));
    }
    PRegister RegDiscrete(int addr, RegisterFormat fmt = U8) {
        return TRegister::Intern(
            Device, TRegisterConfig::Create(
            TFakeSerialDevice::REG_DISCRETE, addr, fmt, 1, 0, true, true, "discrete"));
    }
    PRegister RegHolding(int addr, RegisterFormat fmt = U16, double scale = 1, 
        double offset = 0, EWordOrder word_order = EWordOrder::BigEndian) {
        return TRegister::Intern(
            Device, TRegisterConfig::Create(
            TFakeSerialDevice::REG_HOLDING, addr, fmt, scale, offset, true, false,
            "holding", false, 0, word_order));
    }
    PRegister RegInput(int addr, RegisterFormat fmt = U16, double scale = 1,
        double offset = 0, EWordOrder word_order = EWordOrder::BigEndian) {
        return TRegister::Intern(
            Device, TRegisterConfig::Create(
            TFakeSerialDevice::REG_INPUT, addr, fmt, scale, offset, true, true,
            "input", false, 0, word_order));
    }
    PFakeSerialPort Port;
    PSerialClient SerialClient;
    PFakeSerialDevice Device;
};

void TSerialClientTest::SetUp()
{
    TLoggedFixture::SetUp();
    Port = std::make_shared<TFakeSerialPort>(*this);
    SerialClient = std::make_shared<TSerialClient>(Port);
#if 0
    SerialClient->SetModbusDebug(true);
#endif
    try {
        auto config = std::make_shared<TDeviceConfig>("fake_sample", std::to_string(1), "fake");
        config->MaxReadRegisters = 0;
        Device = std::dynamic_pointer_cast<TFakeSerialDevice>(SerialClient->CreateDevice(config));
    } catch (const TSerialDeviceException &e) {}
    SerialClient->SetReadCallback([this](PRegister reg, bool changed) {
            Emit() << "Read Callback: " << reg->ToString() << " becomes " <<
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
            Emit() << "Error Callback: " << reg->ToString() << ": " << what;
        });
}

void TSerialClientTest::TearDown()
{
    SerialClient.reset();
    TLoggedFixture::TearDown();
    TRegister::DeleteIntern();
}


TEST_F(TSerialClientTest, Poll)
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

    Device->Coils[1] = 1;
    Device->Discrete[10] = 1;
    Device->Holding[22] = 4242;
    Device->Input[33] = 42000;

    Note() << "Cycle()";
    SerialClient->Cycle();

    EXPECT_EQ(to_string(0), SerialClient->GetTextValue(coil0));
    EXPECT_EQ(to_string(1), SerialClient->GetTextValue(coil1));
    EXPECT_EQ(to_string(1), SerialClient->GetTextValue(discrete10));
    EXPECT_EQ(to_string(4242), SerialClient->GetTextValue(holding22));
    EXPECT_EQ(to_string(42000), SerialClient->GetTextValue(input33));
}

TEST_F(TSerialClientTest, Write)
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
        Note() << "Cycle()";
        SerialClient->Cycle();

        EXPECT_EQ(to_string(1), SerialClient->GetTextValue(coil1));
        EXPECT_EQ(1, Device->Coils[1]);
        EXPECT_EQ(to_string(4242), SerialClient->GetTextValue(holding20));
        EXPECT_EQ(4242, Device->Holding[20]);
    }
}

TEST_F(TSerialClientTest, S8)
{
    PRegister holding20 = RegHolding(20, S8);
    PRegister input30 = RegInput(30, S8);
    SerialClient->AddRegister(holding20);
    SerialClient->AddRegister(input30);

    Note() << "server -> client: 10, 20";
    Device->Holding[20] = 10;
    Device->Input[30] = 20;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(to_string(20), SerialClient->GetTextValue(input30));

    Note() << "server -> client: -2, -3";
    Device->Holding[20] = 254;
    Device->Input[30] = 253;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(to_string(-3), SerialClient->GetTextValue(input30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(10, Device->Holding[20]);

    Note() << "client -> server: -2";
    SerialClient->SetTextValue(holding20, "-2");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(254, Device->Holding[20]);
}

TEST_F(TSerialClientTest, Char8)
{
    PRegister holding20 = RegHolding(20, Char8);
    PRegister input30 = RegInput(30, Char8);
    SerialClient->AddRegister(holding20);
    SerialClient->AddRegister(input30);

    Note() << "server -> client: 65, 66";
    Device->Holding[20] = 65;
    Device->Input[30] = 66;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("A", SerialClient->GetTextValue(holding20));
    EXPECT_EQ("B", SerialClient->GetTextValue(input30));

    Note() << "client -> server: '!'";
    SerialClient->SetTextValue(holding20, "!");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("!", SerialClient->GetTextValue(holding20));
    EXPECT_EQ(33, Device->Holding[20]);
}

TEST_F(TSerialClientTest, S64)
{
    PRegister holding20 = RegHolding(20, S64);
    PRegister input30 = RegInput(30, S64);
    SerialClient->AddRegister(holding20);
    SerialClient->AddRegister(input30);

    Note() << "server -> client: 10, 20";
    Device->Holding[20] = 0x00AA;
    Device->Holding[21] = 0x00BB;
    Device->Holding[22] = 0x00CC;
    Device->Holding[23] = 0x00DD;
    Device->Input[30] = 0xFFFF;
    Device->Input[31] = 0xFFFF;
    Device->Input[32] = 0xFFFF;
    Device->Input[33] = 0xFFFF;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB00CC00DD), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(to_string(-1), SerialClient->GetTextValue(input30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0, Device->Holding[20]);
    EXPECT_EQ(0, Device->Holding[21]);
    EXPECT_EQ(0, Device->Holding[22]);
    EXPECT_EQ(10, Device->Holding[23]);

    Note() << "client -> server: -2";
    SerialClient->SetTextValue(holding20, "-2");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0xFFFF, Device->Holding[20]);
    EXPECT_EQ(0xFFFF, Device->Holding[21]);
    EXPECT_EQ(0xFFFF, Device->Holding[22]);
    EXPECT_EQ(0xFFFE, Device->Holding[23]);
}

TEST_F(TSerialClientTest, U64)
{
    PRegister holding20 = RegHolding(20, U64);
    PRegister input30 = RegInput(30, U64);
    SerialClient->AddRegister(holding20);
    SerialClient->AddRegister(input30);

    Note() << "server -> client: 10, 20";
    Device->Holding[20] = 0x00AA;
    Device->Holding[21] = 0x00BB;
    Device->Holding[22] = 0x00CC;
    Device->Holding[23] = 0x00DD;
    Device->Input[30] = 0xFFFF;
    Device->Input[31] = 0xFFFF;
    Device->Input[32] = 0xFFFF;
    Device->Input[33] = 0xFFFF;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB00CC00DD), SerialClient->GetTextValue(holding20));
    EXPECT_EQ("18446744073709551615", SerialClient->GetTextValue(input30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0, Device->Holding[20]);
    EXPECT_EQ(0, Device->Holding[21]);
    EXPECT_EQ(0, Device->Holding[22]);
    EXPECT_EQ(10, Device->Holding[23]);

    Note() << "client -> server: -2";
    SerialClient->SetTextValue(holding20, "-2");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("18446744073709551614", SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0xFFFF, Device->Holding[20]);
    EXPECT_EQ(0xFFFF, Device->Holding[21]);
    EXPECT_EQ(0xFFFF, Device->Holding[22]);
    EXPECT_EQ(0xFFFE, Device->Holding[23]);
}

TEST_F(TSerialClientTest, S32)
{
    PRegister holding20 = RegHolding(20, S32);
    PRegister input30 = RegInput(30, S32);
    SerialClient->AddRegister(holding20);
    SerialClient->AddRegister(input30);

    // create scaled register
    PRegister holding24 = RegHolding(24, S32, 0.001);
    SerialClient->AddRegister(holding24);

    Note() << "server -> client: 10, 20";
    Device->Holding[20] = 0x00AA;
    Device->Holding[21] = 0x00BB;
    Device->Input[30] = 0xFFFF;
    Device->Input[31] = 0xFFFF;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(to_string(-1), SerialClient->GetTextValue(input30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0, Device->Holding[20]);
    EXPECT_EQ(10, Device->Holding[21]);

    Note() << "client -> server: -2";
    SerialClient->SetTextValue(holding20, "-2");
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(holding20));
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0xFFFF, Device->Holding[20]);
    EXPECT_EQ(0xFFFE, Device->Holding[21]);

    Note() << "client -> server: -0.123 (scaled)";
    SerialClient->SetTextValue(holding24, "-0.123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", SerialClient->GetTextValue(holding24));
    EXPECT_EQ(0xffff, Device->Holding[24]);
    EXPECT_EQ(0xff85, Device->Holding[25]);

    Note() << "server -> client: 0xffff 0xff85 (scaled)";
    Device->Holding[24] = 0xffff;
    Device->Holding[25] = 0xff85;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", SerialClient->GetTextValue(holding24));
}

TEST_F(TSerialClientTest, WordSwap)
{
    PRegister holding20 = RegHolding(20, S32, 1, 0, EWordOrder::LittleEndian);
    PRegister holding24 = RegHolding(24, U64, 1, 0, EWordOrder::LittleEndian);
    SerialClient->AddRegister(holding24);
    SerialClient->AddRegister(holding20);

    Note() << "server -> client: 0x00BB, 0x00AA";
    Device->Holding[20] = 0x00BB;
    Device->Holding[21] = 0x00AA;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB), SerialClient->GetTextValue(holding20));

    Note() << "client -> server: -2";
    SerialClient->SetTextValue(holding20, "-2");
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(holding20));
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0xFFFE, Device->Holding[20]);
    EXPECT_EQ(0xFFFF, Device->Holding[21]);

    // U64
    Note() << "client -> server: 10";
    SerialClient->SetTextValue(holding24, "47851549213065437"); // 0x00AA00BB00CC00DD
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB00CC00DD), SerialClient->GetTextValue(holding24));
    EXPECT_EQ(0x00DD, Device->Holding[24]);
    EXPECT_EQ(0x00CC, Device->Holding[25]);
    EXPECT_EQ(0x00BB, Device->Holding[26]);
    EXPECT_EQ(0x00AA, Device->Holding[27]);

}

TEST_F(TSerialClientTest, U32)
{
    PRegister holding20 = RegHolding(20, U32);
    PRegister input30 = RegInput(30, U32);
    SerialClient->AddRegister(holding20);
    SerialClient->AddRegister(input30);

    Note() << "server -> client: 10, 20";
    Device->Holding[20] = 0x00AA;
    Device->Holding[21] = 0x00BB;
    Device->Input[30] = 0xFFFF;
    Device->Input[31] = 0xFFFF;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(to_string(0xFFFFFFFF), SerialClient->GetTextValue(input30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0, Device->Holding[20]);
    EXPECT_EQ(10, Device->Holding[21]);

    Note() << "client -> server: -1 (overflow)";
    SerialClient->SetTextValue(holding20, "-1");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0xFFFFFFFF), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0xFFFF, Device->Holding[20]);
    EXPECT_EQ(0xFFFF, Device->Holding[21]);

    Device->Holding[22] = 123;
    Device->Holding[23] = 123;
    Note() << "client -> server: 4294967296 (overflow)";
    SerialClient->SetTextValue(holding20, "4294967296");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0, Device->Holding[20]);
    EXPECT_EQ(0, Device->Holding[21]);

    //boundaries check
    EXPECT_EQ(123, Device->Holding[22]);
    EXPECT_EQ(123, Device->Holding[23]);
}

TEST_F(TSerialClientTest, BCD32)
{
    PRegister holding20 = RegHolding(20, BCD32);
    SerialClient->AddRegister(holding20);
    Device->Holding[22] = 123;
    Device->Holding[23] = 123;

    Note() << "server -> client: 0x1234 0x5678";
    Device->Holding[20] = 0x1234;
    Device->Holding[21] = 0x5678;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(12345678), SerialClient->GetTextValue(holding20));

    Note() << "client -> server: 12345678";
    SerialClient->SetTextValue(holding20, "12345678");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(12345678), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0x1234, Device->Holding[20]);
    EXPECT_EQ(0x5678, Device->Holding[21]);

    Note() << "client -> server: 567890";
    SerialClient->SetTextValue(holding20, "567890");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(567890), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0x0056, Device->Holding[20]);
    EXPECT_EQ(0x7890, Device->Holding[21]);

    //boundaries check
    EXPECT_EQ(123, Device->Holding[22]);
    EXPECT_EQ(123, Device->Holding[23]);
}

TEST_F(TSerialClientTest, BCD24)
{
    PRegister holding20 = RegHolding(20, BCD24);
    SerialClient->AddRegister(holding20);
    Device->Holding[22] = 123;
    Device->Holding[23] = 123;

    Note() << "server -> client: 0x0034 0x5678";
    Device->Holding[20] = 0x0034;
    Device->Holding[21] = 0x5678;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(345678), SerialClient->GetTextValue(holding20));

    Note() << "client -> server: 567890";
    SerialClient->SetTextValue(holding20, "567890");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(567890), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0x0056, Device->Holding[20]);
    EXPECT_EQ(0x7890, Device->Holding[21]);

    //boundaries check
    EXPECT_EQ(123, Device->Holding[22]);
    EXPECT_EQ(123, Device->Holding[23]);
}

TEST_F(TSerialClientTest, BCD16)
{
    PRegister holding20 = RegHolding(20, BCD16);
    SerialClient->AddRegister(holding20);
    Device->Holding[21] = 123;

    Note() << "server -> client: 0x1234";
    Device->Holding[20] = 0x1234;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(1234), SerialClient->GetTextValue(holding20));

    Note() << "client -> server: 1234";
    SerialClient->SetTextValue(holding20, "1234");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(1234), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0x1234, Device->Holding[20]);

    //boundaries check
    EXPECT_EQ(123, Device->Holding[21]);
}

TEST_F(TSerialClientTest, BCD8)
{
    PRegister holding20 = RegHolding(20, BCD8);
    SerialClient->AddRegister(holding20);
    Device->Holding[21] = 123;

    Note() << "server -> client: 0x12";
    Device->Holding[20] = 0x12;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(12), SerialClient->GetTextValue(holding20));

    Note() << "client -> server: 12";
    SerialClient->SetTextValue(holding20, "12");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(12), SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0x12, Device->Holding[20]);

    //boundaries check
    EXPECT_EQ(123, Device->Holding[21]);
}

TEST_F(TSerialClientTest, Float32)
{
	// create scaled register
    PRegister holding24 = RegHolding(24, Float, 100);
    SerialClient->AddRegister(holding24);

    PRegister holding20 = RegHolding(20, Float);
    PRegister input30 = RegInput(30, Float);
    SerialClient->AddRegister(holding20);
    SerialClient->AddRegister(input30);

    Note() << "server -> client: 0x45d2 0x0000, 0x449d 0x8000";
    Device->Holding[20] = 0x45d2;
    Device->Holding[21] = 0x0000;
    Device->Input[30] = 0x449d;
    Device->Input[31] = 0x8000;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("6720", SerialClient->GetTextValue(holding20));
    EXPECT_EQ("1260", SerialClient->GetTextValue(input30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(holding20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("10", SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0x4120, Device->Holding[20]);
    EXPECT_EQ(0x0000, Device->Holding[21]);

    Note() << "client -> server: -0.00123";
    SerialClient->SetTextValue(holding20, "-0.00123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.00123", SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0xbaa1, Device->Holding[20]);
    EXPECT_EQ(0x37f4, Device->Holding[21]);

    Note() << "client -> server: -0.123 (scaled)";
    SerialClient->SetTextValue(holding24, "-0.123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", SerialClient->GetTextValue(holding24));
    EXPECT_EQ(0xbaa1, Device->Holding[24]);
    EXPECT_EQ(0x37f4, Device->Holding[25]);

    Note() << "server -> client: 0x449d 0x8000 (scaled)";
    Device->Holding[24] = 0x449d;
    Device->Holding[25] = 0x8000;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("126000", SerialClient->GetTextValue(holding24));
}

TEST_F(TSerialClientTest, Double64)
{
	// create scaled register
    PRegister holding24 = RegHolding(24, Double, 100);
    SerialClient->AddRegister(holding24);

    PRegister holding20 = RegHolding(20, Double);
    PRegister input30 = RegInput(30, Double);
    SerialClient->AddRegister(holding20);
    SerialClient->AddRegister(input30);

    Note() << "server -> client: 40ba401f7ced9168 , 4093b148b4395810";
    Device->Holding[20] = 0x40ba;
    Device->Holding[21] = 0x401f;
    Device->Holding[22] = 0x7ced;
    Device->Holding[23] = 0x9168;

    Device->Input[30] = 0x4093;
    Device->Input[31] = 0xb148;
    Device->Input[32] = 0xb439;
    Device->Input[33] = 0x5810;

    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("6720.123", SerialClient->GetTextValue(holding20));
    EXPECT_EQ("1260.321", SerialClient->GetTextValue(input30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(holding20, "10.9999");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("10.9999", SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0x4025, Device->Holding[20]);
    EXPECT_EQ(0xfff2, Device->Holding[21]);
    EXPECT_EQ(0xe48e, Device->Holding[22]);
    EXPECT_EQ(0x8a72, Device->Holding[23]);

    Note() << "client -> server: -0.00123";
    SerialClient->SetTextValue(holding20, "-0.00123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.00123", SerialClient->GetTextValue(holding20));
    EXPECT_EQ(0xbf54, Device->Holding[20]);
    EXPECT_EQ(0x26fe, Device->Holding[21]);
    EXPECT_EQ(0x718a, Device->Holding[22]);
    EXPECT_EQ(0x86d7, Device->Holding[23]);

    Note() << "client -> server: -0.123 (scaled)";
    SerialClient->SetTextValue(holding24, "-0.123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", SerialClient->GetTextValue(holding24));
    EXPECT_EQ(0xbf54, Device->Holding[24]);
    EXPECT_EQ(0x26fe, Device->Holding[25]);
    EXPECT_EQ(0x718a, Device->Holding[26]);
    EXPECT_EQ(0x86d7, Device->Holding[27]);

    Note() << "server -> client: 4093b00000000000 (scaled)";
    Device->Holding[24] = 0x4093;
    Device->Holding[25] = 0xb000;
    Device->Holding[26] = 0x0000;
    Device->Holding[27] = 0x0000;

    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("126000", SerialClient->GetTextValue(holding24));
}

TEST_F(TSerialClientTest, offset)
{
    // create scaled register with offset
    PRegister holding24 = RegHolding(24, S16, 3, -15);
    SerialClient->AddRegister(holding24);

    Note() << "client -> server: -87 (scaled)";
    SerialClient->SetTextValue(holding24, "-87");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-87", SerialClient->GetTextValue(holding24));
    EXPECT_EQ(0xffe8, Device->Holding[24]);
}

TEST_F(TSerialClientTest, Errors)
{
    PRegister holding20 = RegHolding(20);
    Device->BlockReadFor(TFakeSerialDevice::REG_HOLDING, 20, true);
    Device->BlockWriteFor(TFakeSerialDevice::REG_HOLDING, 20, true);
    SerialClient->AddRegister(holding20);

    for (int i = 0; i < 3; i++) {
        Note() << "Cycle() [read, rw blacklisted]";
        SerialClient->Cycle();
    }

    SerialClient->SetTextValue(holding20, "42");
    Note() << "Cycle() [write, rw blacklisted]";
    SerialClient->Cycle();

    Device->BlockWriteFor(TFakeSerialDevice::REG_HOLDING, 20, false);
    SerialClient->SetTextValue(holding20, "42");
    Note() << "Cycle() [write, r blacklisted]";
    SerialClient->Cycle();

    Device->BlockWriteFor(TFakeSerialDevice::REG_HOLDING, 20, true);
    SerialClient->SetTextValue(holding20, "43");
    Note() << "Cycle() [write, rw blacklisted]";
    SerialClient->Cycle();

    Device->BlockReadFor(TFakeSerialDevice::REG_HOLDING, 20, false);
    Note() << "Cycle() [write, w blacklisted]";
    SerialClient->Cycle();

    Device->BlockWriteFor(TFakeSerialDevice::REG_HOLDING, 20, false);
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


class TSerialClientIntegrationTest: public TSerialClientTest
{
protected:
    void SetUp();
    void FilterConfig(const std::string& device_name);

    PFakeMQTTClient MQTTClient;
    PHandlerConfig Config;
};

void TSerialClientIntegrationTest::SetUp()
{
    TSerialClientTest::SetUp();
    MQTTClient = PFakeMQTTClient(new TFakeMQTTClient("serial-client-integration-test", *this));
    TConfigParser parser(GetDataFilePath("configs/config-test.json"), false,
                         TSerialDeviceFactory::GetRegisterTypes);
    Config = parser.Parse();
}

void TSerialClientIntegrationTest::FilterConfig(const std::string& device_name)
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

TEST_F(TSerialClientIntegrationTest, OnValue)
{
    FilterConfig("OnValueTest");

    PMQTTSerialObserver observer(new TMQTTSerialObserver(MQTTClient, Config, Port));
    observer->SetUp();

    Device = std::dynamic_pointer_cast<TFakeSerialDevice>(TSerialDeviceFactory::GetDevice("0x90", "fake"));

    if (!Device) {
        throw std::runtime_error("device not found or wrong type");
    }

    Device->Holding[0] = 0;
    Note() << "LoopOnce()";
    observer->LoopOnce();

    MQTTClient->DoPublish(true, 0, "/devices/OnValueTest/controls/Relay 1/on", "1");

    Note() << "LoopOnce()";
    observer->LoopOnce();
    ASSERT_EQ(500, Device->Holding[0]);
}


TEST_F(TSerialClientIntegrationTest, WordSwap)
{
    FilterConfig("WordsLETest");

    PMQTTSerialObserver observer(new TMQTTSerialObserver(MQTTClient, Config, Port));
    observer->SetUp();

    Device = std::dynamic_pointer_cast<TFakeSerialDevice>(TSerialDeviceFactory::GetDevice("0x91", "fake"));

    if (!Device) {
        throw std::runtime_error("device not found or wrong type");
    }

    Device->Holding[0] = 0;
    Note() << "LoopOnce()";
    observer->LoopOnce();

    MQTTClient->DoPublish(true, 0, "/devices/WordsLETest/controls/Voltage/on", "123");

    Note() << "LoopOnce()";
    observer->LoopOnce();
    ASSERT_EQ(123, Device->Holding[0]);
    ASSERT_EQ(0, Device->Holding[1]);
    ASSERT_EQ(0, Device->Holding[2]);
    ASSERT_EQ(0, Device->Holding[3]);
}

TEST_F(TSerialClientIntegrationTest, Errors)
{
    FilterConfig("DDL24");

    PMQTTSerialObserver observer(new TMQTTSerialObserver(MQTTClient, Config, Port));
    observer->SetUp();

    Device = std::dynamic_pointer_cast<TFakeSerialDevice>(TSerialDeviceFactory::GetDevice("23", "fake"));

    if (!Device) {
        throw std::runtime_error("device not found or wrong type");
    }

    Device->BlockReadFor(TFakeSerialDevice::REG_HOLDING, 4, true);
    Device->BlockWriteFor(TFakeSerialDevice::REG_HOLDING, 4, true);
    Device->BlockReadFor(TFakeSerialDevice::REG_HOLDING, 7, true);
    Device->BlockWriteFor(TFakeSerialDevice::REG_HOLDING, 7, true);

    Note() << "LoopOnce() [read, rw blacklisted]";
    observer->LoopOnce();

    MQTTClient->DoPublish(true, 0, "/devices/ddl24/controls/RGB/on", "10;20;30");
    MQTTClient->DoPublish(true, 0, "/devices/ddl24/controls/White/on", "42");

    Note() << "LoopOnce() [write, rw blacklisted]";
    observer->LoopOnce();

    Device->BlockReadFor(TFakeSerialDevice::REG_HOLDING, 4, false);
    Device->BlockWriteFor(TFakeSerialDevice::REG_HOLDING, 4, false);
    Device->BlockReadFor(TFakeSerialDevice::REG_HOLDING, 7, false);
    Device->BlockWriteFor(TFakeSerialDevice::REG_HOLDING, 7, false);

    Note() << "LoopOnce() [read, nothing blacklisted]";
    observer->LoopOnce();

    MQTTClient->DoPublish(true, 0, "/devices/ddl24/controls/RGB/on", "10;20;30");
    MQTTClient->DoPublish(true, 0, "/devices/ddl24/controls/White/on", "42");

    Note() << "LoopOnce() [write, nothing blacklisted]";
    observer->LoopOnce();

    Note() << "LoopOnce() [read, nothing blacklisted] (2)";
    observer->LoopOnce();
}


class TConfigParserTest: public TLoggedFixture {};

TEST_F(TConfigParserTest, Parse)
{
    TConfigTemplateParser device_parser(GetDataFilePath("device-templates/"), false);
    TConfigParser parser(GetDataFilePath("configs/parse_test.json"), false,
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
            Emit() << "MaxRegHole: " << device_config->MaxRegHole;
            Emit() << "MaxBitHole: " << device_config->MaxBitHole;
            Emit() << "MaxReadRegisters: " << device_config->MaxReadRegisters;
            Emit() << "GuardInterval: " << device_config->GuardInterval.count();
            if (!device_config->DeviceChannelConfigs.empty()) {
                Emit() << "DeviceChannels:";
                for (auto device_channel: device_config->DeviceChannelConfigs) {
                    TTestLogIndent indent(*this);
                    Emit() << "------";
                    Emit() << "Name: " << device_channel->Name;
                    Emit() << "Type: " << device_channel->Type;
                    Emit() << "DeviceId: " << device_channel->DeviceId;
                    Emit() << "Order: " << device_channel->Order;
                    Emit() << "OnValue: " << device_channel->OnValue;
                    Emit() << "Max: " << device_channel->Max;
                    Emit() << "ReadOnly: " << device_channel->ReadOnly;
                    std::stringstream s;
                    bool first = true;
                    for (auto reg: device_channel->RegisterConfigs) {
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
    TConfigParser parser(GetDataFilePath("configs/config-test.json"), true,
                         TSerialDeviceFactory::GetRegisterTypes);
    PHandlerConfig config = parser.Parse();
    ASSERT_TRUE(config->Debug);
}

// TBD: the code must check mosquitto return values
