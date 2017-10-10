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
    PRegister Reg(int addr, RegisterFormat fmt = U16, double scale = 1, 
        double offset = 0, double round_to = 0, EWordOrder word_order = EWordOrder::BigEndian) {
        return TRegister::Intern(
            Device, TRegisterConfig::Create(
            TFakeSerialDevice::REG_FAKE, addr, fmt, scale, offset, round_to, true, false,
            "fake", false, 0, word_order));
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

    PRegister reg0 = Reg(0, U8);
    PRegister reg1 = Reg(1, U8);
    PRegister discrete10 = Reg(10);
    PRegister reg22 = Reg(22);
    PRegister reg33 = Reg(33);

    SerialClient->AddRegister(reg0);
    SerialClient->AddRegister(reg1);
    SerialClient->AddRegister(discrete10);
    SerialClient->AddRegister(reg22);
    SerialClient->AddRegister(reg33);

    Note() << "Cycle()";
    SerialClient->Cycle();

    Device->Registers[1] = 1;
    Device->Registers[10] = 1;
    Device->Registers[22] = 4242;
    Device->Registers[33] = 42000;

    Note() << "Cycle()";
    SerialClient->Cycle();

    EXPECT_EQ(to_string(0), SerialClient->GetTextValue(reg0));
    EXPECT_EQ(to_string(1), SerialClient->GetTextValue(reg1));
    EXPECT_EQ(to_string(1), SerialClient->GetTextValue(discrete10));
    EXPECT_EQ(to_string(4242), SerialClient->GetTextValue(reg22));
    EXPECT_EQ(to_string(42000), SerialClient->GetTextValue(reg33));
}

TEST_F(TSerialClientTest, Write)
{
    PRegister reg1 = Reg(1);
    PRegister reg20 = Reg(20);
    SerialClient->AddRegister(reg1);
    SerialClient->AddRegister(reg20);

    Note() << "Cycle()";
    SerialClient->Cycle();

    SerialClient->SetTextValue(reg1, "1");
    SerialClient->SetTextValue(reg20, "4242");

    for (int i = 0; i < 3; ++i) {
        Note() << "Cycle()";
        SerialClient->Cycle();

        EXPECT_EQ(to_string(1), SerialClient->GetTextValue(reg1));
        EXPECT_EQ(1, Device->Registers[1]);
        EXPECT_EQ(to_string(4242), SerialClient->GetTextValue(reg20));
        EXPECT_EQ(4242, Device->Registers[20]);
    }
}

TEST_F(TSerialClientTest, S8)
{
    PRegister reg20 = Reg(20, S8);
    PRegister reg30 = Reg(30, S8);
    SerialClient->AddRegister(reg20);
    SerialClient->AddRegister(reg30);

    Note() << "server -> client: 10, 20";
    Device->Registers[20] = 10;
    Device->Registers[30] = 20;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(to_string(20), SerialClient->GetTextValue(reg30));

    Note() << "server -> client: -2, -3";
    Device->Registers[20] = 254;
    Device->Registers[30] = 253;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(to_string(-3), SerialClient->GetTextValue(reg30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(reg20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(10, Device->Registers[20]);

    Note() << "client -> server: -2";
    SerialClient->SetTextValue(reg20, "-2");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(254, Device->Registers[20]);
}

TEST_F(TSerialClientTest, Char8)
{
    PRegister reg20 = Reg(20, Char8);
    PRegister reg30 = Reg(30, Char8);
    SerialClient->AddRegister(reg20);
    SerialClient->AddRegister(reg30);

    Note() << "server -> client: 65, 66";
    Device->Registers[20] = 65;
    Device->Registers[30] = 66;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("A", SerialClient->GetTextValue(reg20));
    EXPECT_EQ("B", SerialClient->GetTextValue(reg30));

    Note() << "client -> server: '!'";
    SerialClient->SetTextValue(reg20, "!");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("!", SerialClient->GetTextValue(reg20));
    EXPECT_EQ(33, Device->Registers[20]);
}

TEST_F(TSerialClientTest, S64)
{
    PRegister reg20 = Reg(20, S64);
    PRegister reg30 = Reg(30, S64);
    SerialClient->AddRegister(reg20);
    SerialClient->AddRegister(reg30);

    Note() << "server -> client: 10, 20";
    Device->Registers[20] = 0x00AA;
    Device->Registers[21] = 0x00BB;
    Device->Registers[22] = 0x00CC;
    Device->Registers[23] = 0x00DD;
    Device->Registers[30] = 0xFFFF;
    Device->Registers[31] = 0xFFFF;
    Device->Registers[32] = 0xFFFF;
    Device->Registers[33] = 0xFFFF;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB00CC00DD), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(to_string(-1), SerialClient->GetTextValue(reg30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(reg20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0, Device->Registers[20]);
    EXPECT_EQ(0, Device->Registers[21]);
    EXPECT_EQ(0, Device->Registers[22]);
    EXPECT_EQ(10, Device->Registers[23]);

    Note() << "client -> server: -2";
    SerialClient->SetTextValue(reg20, "-2");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0xFFFF, Device->Registers[20]);
    EXPECT_EQ(0xFFFF, Device->Registers[21]);
    EXPECT_EQ(0xFFFF, Device->Registers[22]);
    EXPECT_EQ(0xFFFE, Device->Registers[23]);
}

TEST_F(TSerialClientTest, U64)
{
    PRegister reg20 = Reg(20, U64);
    PRegister reg30 = Reg(30, U64);
    SerialClient->AddRegister(reg20);
    SerialClient->AddRegister(reg30);

    Note() << "server -> client: 10, 20";
    Device->Registers[20] = 0x00AA;
    Device->Registers[21] = 0x00BB;
    Device->Registers[22] = 0x00CC;
    Device->Registers[23] = 0x00DD;
    Device->Registers[30] = 0xFFFF;
    Device->Registers[31] = 0xFFFF;
    Device->Registers[32] = 0xFFFF;
    Device->Registers[33] = 0xFFFF;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB00CC00DD), SerialClient->GetTextValue(reg20));
    EXPECT_EQ("18446744073709551615", SerialClient->GetTextValue(reg30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(reg20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0, Device->Registers[20]);
    EXPECT_EQ(0, Device->Registers[21]);
    EXPECT_EQ(0, Device->Registers[22]);
    EXPECT_EQ(10, Device->Registers[23]);

    Note() << "client -> server: -2";
    SerialClient->SetTextValue(reg20, "-2");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("18446744073709551614", SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0xFFFF, Device->Registers[20]);
    EXPECT_EQ(0xFFFF, Device->Registers[21]);
    EXPECT_EQ(0xFFFF, Device->Registers[22]);
    EXPECT_EQ(0xFFFE, Device->Registers[23]);
}

TEST_F(TSerialClientTest, S32)
{
    PRegister reg20 = Reg(20, S32);
    PRegister reg30 = Reg(30, S32);
    SerialClient->AddRegister(reg20);
    SerialClient->AddRegister(reg30);

    // create scaled register
    PRegister reg24 = Reg(24, S32, 0.001);
    SerialClient->AddRegister(reg24);

    Note() << "server -> client: 10, 20";
    Device->Registers[20] = 0x00AA;
    Device->Registers[21] = 0x00BB;
    Device->Registers[30] = 0xFFFF;
    Device->Registers[31] = 0xFFFF;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(to_string(-1), SerialClient->GetTextValue(reg30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(reg20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0, Device->Registers[20]);
    EXPECT_EQ(10, Device->Registers[21]);

    Note() << "client -> server: -2";
    SerialClient->SetTextValue(reg20, "-2");
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(reg20));
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0xFFFF, Device->Registers[20]);
    EXPECT_EQ(0xFFFE, Device->Registers[21]);

    Note() << "client -> server: -0.123 (scaled)";
    SerialClient->SetTextValue(reg24, "-0.123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", SerialClient->GetTextValue(reg24));
    EXPECT_EQ(0xffff, Device->Registers[24]);
    EXPECT_EQ(0xff85, Device->Registers[25]);

    Note() << "server -> client: 0xffff 0xff85 (scaled)";
    Device->Registers[24] = 0xffff;
    Device->Registers[25] = 0xff85;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", SerialClient->GetTextValue(reg24));
}

TEST_F(TSerialClientTest, WordSwap)
{
    PRegister reg20 = Reg(20, S32, 1, 0, 0, EWordOrder::LittleEndian);
    PRegister reg24 = Reg(24, U64, 1, 0, 0, EWordOrder::LittleEndian);
    SerialClient->AddRegister(reg24);
    SerialClient->AddRegister(reg20);

    Note() << "server -> client: 0x00BB, 0x00AA";
    Device->Registers[20] = 0x00BB;
    Device->Registers[21] = 0x00AA;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB), SerialClient->GetTextValue(reg20));

    Note() << "client -> server: -2";
    SerialClient->SetTextValue(reg20, "-2");
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(reg20));
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0xFFFE, Device->Registers[20]);
    EXPECT_EQ(0xFFFF, Device->Registers[21]);

    // U64
    Note() << "client -> server: 10";
    SerialClient->SetTextValue(reg24, "47851549213065437"); // 0x00AA00BB00CC00DD
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB00CC00DD), SerialClient->GetTextValue(reg24));
    EXPECT_EQ(0x00DD, Device->Registers[24]);
    EXPECT_EQ(0x00CC, Device->Registers[25]);
    EXPECT_EQ(0x00BB, Device->Registers[26]);
    EXPECT_EQ(0x00AA, Device->Registers[27]);

}

TEST_F(TSerialClientTest, U32)
{
    PRegister reg20 = Reg(20, U32);
    PRegister reg30 = Reg(30, U32);
    SerialClient->AddRegister(reg20);
    SerialClient->AddRegister(reg30);

    Note() << "server -> client: 10, 20";
    Device->Registers[20] = 0x00AA;
    Device->Registers[21] = 0x00BB;
    Device->Registers[30] = 0xFFFF;
    Device->Registers[31] = 0xFFFF;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(to_string(0xFFFFFFFF), SerialClient->GetTextValue(reg30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(reg20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0, Device->Registers[20]);
    EXPECT_EQ(10, Device->Registers[21]);

    Note() << "client -> server: -1 (overflow)";
    SerialClient->SetTextValue(reg20, "-1");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0xFFFFFFFF), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0xFFFF, Device->Registers[20]);
    EXPECT_EQ(0xFFFF, Device->Registers[21]);

    Device->Registers[22] = 123;
    Device->Registers[23] = 123;
    Note() << "client -> server: 4294967296 (overflow)";
    SerialClient->SetTextValue(reg20, "4294967296");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0, Device->Registers[20]);
    EXPECT_EQ(0, Device->Registers[21]);

    //boundaries check
    EXPECT_EQ(123, Device->Registers[22]);
    EXPECT_EQ(123, Device->Registers[23]);
}

TEST_F(TSerialClientTest, BCD32)
{
    PRegister reg20 = Reg(20, BCD32);
    SerialClient->AddRegister(reg20);
    Device->Registers[22] = 123;
    Device->Registers[23] = 123;

    Note() << "server -> client: 0x1234 0x5678";
    Device->Registers[20] = 0x1234;
    Device->Registers[21] = 0x5678;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(12345678), SerialClient->GetTextValue(reg20));

    Note() << "client -> server: 12345678";
    SerialClient->SetTextValue(reg20, "12345678");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(12345678), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0x1234, Device->Registers[20]);
    EXPECT_EQ(0x5678, Device->Registers[21]);

    Note() << "client -> server: 567890";
    SerialClient->SetTextValue(reg20, "567890");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(567890), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0x0056, Device->Registers[20]);
    EXPECT_EQ(0x7890, Device->Registers[21]);

    //boundaries check
    EXPECT_EQ(123, Device->Registers[22]);
    EXPECT_EQ(123, Device->Registers[23]);
}

TEST_F(TSerialClientTest, BCD24)
{
    PRegister reg20 = Reg(20, BCD24);
    SerialClient->AddRegister(reg20);
    Device->Registers[22] = 123;
    Device->Registers[23] = 123;

    Note() << "server -> client: 0x0034 0x5678";
    Device->Registers[20] = 0x0034;
    Device->Registers[21] = 0x5678;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(345678), SerialClient->GetTextValue(reg20));

    Note() << "client -> server: 567890";
    SerialClient->SetTextValue(reg20, "567890");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(567890), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0x0056, Device->Registers[20]);
    EXPECT_EQ(0x7890, Device->Registers[21]);

    //boundaries check
    EXPECT_EQ(123, Device->Registers[22]);
    EXPECT_EQ(123, Device->Registers[23]);
}

TEST_F(TSerialClientTest, BCD16)
{
    PRegister reg20 = Reg(20, BCD16);
    SerialClient->AddRegister(reg20);
    Device->Registers[21] = 123;

    Note() << "server -> client: 0x1234";
    Device->Registers[20] = 0x1234;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(1234), SerialClient->GetTextValue(reg20));

    Note() << "client -> server: 1234";
    SerialClient->SetTextValue(reg20, "1234");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(1234), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0x1234, Device->Registers[20]);

    //boundaries check
    EXPECT_EQ(123, Device->Registers[21]);
}

TEST_F(TSerialClientTest, BCD8)
{
    PRegister reg20 = Reg(20, BCD8);
    SerialClient->AddRegister(reg20);
    Device->Registers[21] = 123;

    Note() << "server -> client: 0x12";
    Device->Registers[20] = 0x12;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(12), SerialClient->GetTextValue(reg20));

    Note() << "client -> server: 12";
    SerialClient->SetTextValue(reg20, "12");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(12), SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0x12, Device->Registers[20]);

    //boundaries check
    EXPECT_EQ(123, Device->Registers[21]);
}

TEST_F(TSerialClientTest, Float32)
{
	// create scaled register
    PRegister reg24 = Reg(24, Float, 100);
    SerialClient->AddRegister(reg24);

    PRegister reg20 = Reg(20, Float);
    PRegister reg30 = Reg(30, Float);
    SerialClient->AddRegister(reg20);
    SerialClient->AddRegister(reg30);

    Note() << "server -> client: 0x45d2 0x0000, 0x449d 0x8000";
    Device->Registers[20] = 0x45d2;
    Device->Registers[21] = 0x0000;
    Device->Registers[30] = 0x449d;
    Device->Registers[31] = 0x8000;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("6720", SerialClient->GetTextValue(reg20));
    EXPECT_EQ("1260", SerialClient->GetTextValue(reg30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(reg20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("10", SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0x4120, Device->Registers[20]);
    EXPECT_EQ(0x0000, Device->Registers[21]);

    Note() << "client -> server: -0.00123";
    SerialClient->SetTextValue(reg20, "-0.00123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.00123", SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0xbaa1, Device->Registers[20]);
    EXPECT_EQ(0x37f4, Device->Registers[21]);

    Note() << "client -> server: -0.123 (scaled)";
    SerialClient->SetTextValue(reg24, "-0.123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", SerialClient->GetTextValue(reg24));
    EXPECT_EQ(0xbaa1, Device->Registers[24]);
    EXPECT_EQ(0x37f4, Device->Registers[25]);

    Note() << "server -> client: 0x449d 0x8000 (scaled)";
    Device->Registers[24] = 0x449d;
    Device->Registers[25] = 0x8000;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("126000", SerialClient->GetTextValue(reg24));
}

TEST_F(TSerialClientTest, Double64)
{
	// create scaled register
    PRegister reg24 = Reg(24, Double, 100);
    SerialClient->AddRegister(reg24);

    PRegister reg20 = Reg(20, Double);
    PRegister reg30 = Reg(30, Double);
    SerialClient->AddRegister(reg20);
    SerialClient->AddRegister(reg30);

    Note() << "server -> client: 40ba401f7ced9168 , 4093b148b4395810";
    Device->Registers[20] = 0x40ba;
    Device->Registers[21] = 0x401f;
    Device->Registers[22] = 0x7ced;
    Device->Registers[23] = 0x9168;

    Device->Registers[30] = 0x4093;
    Device->Registers[31] = 0xb148;
    Device->Registers[32] = 0xb439;
    Device->Registers[33] = 0x5810;

    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("6720.123", SerialClient->GetTextValue(reg20));
    EXPECT_EQ("1260.321", SerialClient->GetTextValue(reg30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(reg20, "10.9999");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("10.9999", SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0x4025, Device->Registers[20]);
    EXPECT_EQ(0xfff2, Device->Registers[21]);
    EXPECT_EQ(0xe48e, Device->Registers[22]);
    EXPECT_EQ(0x8a72, Device->Registers[23]);

    Note() << "client -> server: -0.00123";
    SerialClient->SetTextValue(reg20, "-0.00123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.00123", SerialClient->GetTextValue(reg20));
    EXPECT_EQ(0xbf54, Device->Registers[20]);
    EXPECT_EQ(0x26fe, Device->Registers[21]);
    EXPECT_EQ(0x718a, Device->Registers[22]);
    EXPECT_EQ(0x86d7, Device->Registers[23]);

    Note() << "client -> server: -0.123 (scaled)";
    SerialClient->SetTextValue(reg24, "-0.123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", SerialClient->GetTextValue(reg24));
    EXPECT_EQ(0xbf54, Device->Registers[24]);
    EXPECT_EQ(0x26fe, Device->Registers[25]);
    EXPECT_EQ(0x718a, Device->Registers[26]);
    EXPECT_EQ(0x86d7, Device->Registers[27]);

    Note() << "server -> client: 4093b00000000000 (scaled)";
    Device->Registers[24] = 0x4093;
    Device->Registers[25] = 0xb000;
    Device->Registers[26] = 0x0000;
    Device->Registers[27] = 0x0000;

    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("126000", SerialClient->GetTextValue(reg24));
}

TEST_F(TSerialClientTest, offset)
{
    // create scaled register with offset
    PRegister reg24 = Reg(24, S16, 3, -15);
    SerialClient->AddRegister(reg24);

    Note() << "client -> server: -87 (scaled)";
    SerialClient->SetTextValue(reg24, "-87");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-87", SerialClient->GetTextValue(reg24));
    EXPECT_EQ(0xffe8, Device->Registers[24]);
}

TEST_F(TSerialClientTest, Round)
{
    PRegister reg24_0_01 = Reg(24, Float, 1, 0, 0.01);
    PRegister reg26_1 = Reg(26, Float, 1, 0, 1);
    PRegister reg28_10 = Reg(28, Float, 1, 0, 10);
    PRegister reg30_0_2 = Reg(30, Float, 1, 0, 0.2);
    PRegister reg32_0_01 = Reg(32, Float, 1, 0, 0.01);

    SerialClient->AddRegister(reg24_0_01);
    SerialClient->AddRegister(reg26_1);
    SerialClient->AddRegister(reg28_10);
    SerialClient->AddRegister(reg30_0_2);
    SerialClient->AddRegister(reg32_0_01);

    Note() << "client -> server: 12.345 (not rounded)";
    SerialClient->SetTextValue(reg24_0_01, "12.345");
    SerialClient->SetTextValue(reg26_1, "12.345");
    SerialClient->SetTextValue(reg28_10, "12.345");
    SerialClient->SetTextValue(reg30_0_2, "12.345");
    SerialClient->SetTextValue(reg32_0_01, "12.344");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("12.35", SerialClient->GetTextValue(reg24_0_01));
    EXPECT_EQ("12", SerialClient->GetTextValue(reg26_1));
    EXPECT_EQ("10", SerialClient->GetTextValue(reg28_10));
    EXPECT_EQ("12.4", SerialClient->GetTextValue(reg30_0_2));
    EXPECT_EQ("12.34", SerialClient->GetTextValue(reg32_0_01));

    union {
        uint32_t words;
        float value;
    } data;

    data.words = Device->Read2Registers(24);
    ASSERT_EQ(12.35f, data.value);

    data.words = Device->Read2Registers(26);
    ASSERT_EQ(12.f, data.value);

    data.words = Device->Read2Registers(28);
    ASSERT_EQ(10.f, data.value);

    data.words = Device->Read2Registers(30);
    ASSERT_EQ(12.4f, data.value);

    data.words = Device->Read2Registers(32);
    ASSERT_EQ(12.34f, data.value);
}

TEST_F(TSerialClientTest, Errors)
{
    PRegister reg20 = Reg(20);
    Device->BlockReadFor(20, true);
    Device->BlockWriteFor(20, true);
    SerialClient->AddRegister(reg20);

    for (int i = 0; i < 3; i++) {
        Note() << "Cycle() [read, rw blacklisted]";
        SerialClient->Cycle();
    }

    SerialClient->SetTextValue(reg20, "42");
    Note() << "Cycle() [write, rw blacklisted]";
    SerialClient->Cycle();

    Device->BlockWriteFor(20, false);
    SerialClient->SetTextValue(reg20, "42");
    Note() << "Cycle() [write, r blacklisted]";
    SerialClient->Cycle();

    Device->BlockWriteFor(20, true);
    SerialClient->SetTextValue(reg20, "43");
    Note() << "Cycle() [write, rw blacklisted]";
    SerialClient->Cycle();

    Device->BlockReadFor(20, false);
    Note() << "Cycle() [write, w blacklisted]";
    SerialClient->Cycle();

    Device->BlockWriteFor(20, false);
    SerialClient->SetTextValue(reg20, "42");
    Note() << "Cycle() [write, nothing blacklisted]";
    SerialClient->Cycle();


    reg20->HasErrorValue = true;
    reg20->ErrorValue = 42;
    Note() << "Cycle() [read, set error value for register]";
    SerialClient->Cycle();
    SerialClient->GetTextValue(reg20);

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

    Device->Registers[0] = 0;
    Note() << "LoopOnce()";
    observer->LoopOnce();

    MQTTClient->DoPublish(true, 0, "/devices/OnValueTest/controls/Relay 1/on", "1");

    Note() << "LoopOnce()";
    observer->LoopOnce();
    ASSERT_EQ(500, Device->Registers[0]);
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

    Device->Registers[0] = 0;
    Note() << "LoopOnce()";
    observer->LoopOnce();

    MQTTClient->DoPublish(true, 0, "/devices/WordsLETest/controls/Voltage/on", "123");

    Note() << "LoopOnce()";
    observer->LoopOnce();
    ASSERT_EQ(123, Device->Registers[0]);
    ASSERT_EQ(0, Device->Registers[1]);
    ASSERT_EQ(0, Device->Registers[2]);
    ASSERT_EQ(0, Device->Registers[3]);
}

TEST_F(TSerialClientIntegrationTest, Round)
{
    FilterConfig("RoundTest");

    PMQTTSerialObserver observer(new TMQTTSerialObserver(MQTTClient, Config, Port));
    observer->SetUp();

    Device = std::dynamic_pointer_cast<TFakeSerialDevice>(TSerialDeviceFactory::GetDevice("0x92", "fake"));

    if (!Device) {
        throw std::runtime_error("device not found or wrong type");
    }

    Device->Registers[0] = 0;
    Note() << "LoopOnce()";
    observer->LoopOnce();

    MQTTClient->DoPublish(true, 0, "/devices/RoundTest/controls/Float_0_01/on", "12.345");
    MQTTClient->DoPublish(true, 0, "/devices/RoundTest/controls/Float_1/on", "12.345");
    MQTTClient->DoPublish(true, 0, "/devices/RoundTest/controls/Float_10/on", "12.345");
    MQTTClient->DoPublish(true, 0, "/devices/RoundTest/controls/Float_0_2/on", "12.345");

    Note() << "LoopOnce()";
    observer->LoopOnce();

    union {
        uint32_t words;
        float value;
    } data;

    data.words = Device->Read2Registers(0);
    ASSERT_EQ(12.35f, data.value);

    data.words = Device->Read2Registers(2);
    ASSERT_EQ(12.f, data.value);

    data.words = Device->Read2Registers(4);
    ASSERT_EQ(10.f, data.value);

    data.words = Device->Read2Registers(6);
    ASSERT_EQ(12.4f, data.value);
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

    Device->BlockReadFor(4, true);
    Device->BlockWriteFor(4, true);
    Device->BlockReadFor(7, true);
    Device->BlockWriteFor(7, true);

    Note() << "LoopOnce() [read, rw blacklisted]";
    observer->LoopOnce();

    MQTTClient->DoPublish(true, 0, "/devices/ddl24/controls/RGB/on", "10;20;30");
    MQTTClient->DoPublish(true, 0, "/devices/ddl24/controls/White/on", "42");

    Note() << "LoopOnce() [write, rw blacklisted]";
    observer->LoopOnce();

    Device->BlockReadFor(4, false);
    Device->BlockWriteFor(4, false);
    Device->BlockReadFor(7, false);
    Device->BlockWriteFor(7, false);

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
        Emit() << "ConnSettings: " << port_config->ConnSettings->ToString();
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
