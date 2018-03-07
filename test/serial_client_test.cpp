#include "testlog.h"
#include "serial_observer.h"
#include "fake_mqtt.h"
#include "fake_serial_port.h"
#include "fake_serial_device.h"
#include "tcp_port_settings.h"
#include "virtual_register.h"

#include <gtest/gtest.h>

#include <string>
#include <map>
#include <memory>
#include <algorithm>
#include <cassert>

using namespace std;


class TSerialClientTest: public TLoggedFixture
{
protected:
    void SetUp();
    void TearDown();
    PVirtualRegister Reg(int addr, ERegisterFormat fmt = U16, double scale = 1,
        double offset = 0, double round_to = 0, EWordOrder word_order = EWordOrder::BigEndian) {
        return TVirtualRegister::Create(
            TRegisterConfig::Create(
                TFakeSerialDevice::REG_FAKE, addr, fmt, scale, offset, round_to, true, false,
                "fake", false, 0, word_order), Device, InitContext);
    }
    PFakeSerialPort Port;
    PSerialClient SerialClient;
    PFakeSerialDevice Device;
    TVirtualRegister::TInitContext InitContext;
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
    SerialClient->SetReadCallback([this](const PVirtualRegister & reg) {
            Emit() << "Read Callback: " << reg->ToString() << " becomes " <<
                reg->GetTextValue() << (reg->IsChanged(EPublishData::Value) ? "" : " [unchanged]");
        });
    SerialClient->SetErrorCallback(
        [this](const PVirtualRegister & reg) {
            const char* what;
            switch (reg->GetErrorState()) {
            case EErrorState::WriteError:
                what = "write error";
                break;
            case EErrorState::ReadError:
                what = "read error";
                break;
            case EErrorState::ReadWriteError:
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
    InitContext.clear();
}


TEST_F(TSerialClientTest, Poll)
{
    auto reg0 = Reg(0, U8);
    auto reg1 = Reg(1, U8);
    auto discrete10 = Reg(10);
    auto reg22 = Reg(22);
    auto reg33 = Reg(33);

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

    EXPECT_EQ(to_string(0), reg0->GetTextValue());
    EXPECT_EQ(to_string(1), reg1->GetTextValue());
    EXPECT_EQ(to_string(1), discrete10->GetTextValue());
    EXPECT_EQ(to_string(4242), reg22->GetTextValue());
    EXPECT_EQ(to_string(42000), reg33->GetTextValue());
}

TEST_F(TSerialClientTest, Write)
{
    auto reg1 = Reg(1);
    auto reg20 = Reg(20);
    SerialClient->AddRegister(reg1);
    SerialClient->AddRegister(reg20);

    Note() << "Cycle()";
    SerialClient->Cycle();

    reg1->SetTextValue("1");
    reg20->SetTextValue("4242");

    for (int i = 0; i < 3; ++i) {
        Note() << "Cycle()";
        SerialClient->Cycle();

        EXPECT_EQ(to_string(1), reg1->GetTextValue());
        EXPECT_EQ(1, Device->Registers[1]);
        EXPECT_EQ(to_string(4242), reg20->GetTextValue());
        EXPECT_EQ(4242, Device->Registers[20]);
    }
}

TEST_F(TSerialClientTest, S8)
{
    auto reg20 = Reg(20, S8);
    auto reg30 = Reg(30, S8);
    SerialClient->AddRegister(reg20);
    SerialClient->AddRegister(reg30);

    Note() << "server -> client: 10, 20";
    Device->Registers[20] = 10;
    Device->Registers[30] = 20;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), reg20->GetTextValue());
    EXPECT_EQ(to_string(20), reg30->GetTextValue());

    Note() << "server -> client: -2, -3";
    Device->Registers[20] = 254;
    Device->Registers[30] = 253;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), reg20->GetTextValue());
    EXPECT_EQ(to_string(-3), reg30->GetTextValue());

    Note() << "client -> server: 10";
    reg20->SetTextValue("10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), reg20->GetTextValue());
    EXPECT_EQ(10, Device->Registers[20]);

    Note() << "client -> server: -2";
    reg20->SetTextValue("-2");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), reg20->GetTextValue());
    EXPECT_EQ(254, Device->Registers[20]);
}

TEST_F(TSerialClientTest, Char8)
{
    auto reg20 = Reg(20, Char8);
    auto reg30 = Reg(30, Char8);
    SerialClient->AddRegister(reg20);
    SerialClient->AddRegister(reg30);

    Note() << "server -> client: 65, 66";
    Device->Registers[20] = 65;
    Device->Registers[30] = 66;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("A", reg20->GetTextValue());
    EXPECT_EQ("B", reg30->GetTextValue());

    Note() << "client -> server: '!'";
    reg20->SetTextValue("!");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("!", reg20->GetTextValue());
    EXPECT_EQ(33, Device->Registers[20]);
}

TEST_F(TSerialClientTest, S64)
{
    auto reg20 = Reg(20, S64);
    auto reg30 = Reg(30, S64);
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
    EXPECT_EQ(to_string(0x00AA00BB00CC00DD), reg20->GetTextValue());
    EXPECT_EQ(to_string(-1), reg30->GetTextValue());

    Note() << "client -> server: 10";
    reg20->SetTextValue("10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), reg20->GetTextValue());
    EXPECT_EQ(0, Device->Registers[20]);
    EXPECT_EQ(0, Device->Registers[21]);
    EXPECT_EQ(0, Device->Registers[22]);
    EXPECT_EQ(10, Device->Registers[23]);

    Note() << "client -> server: -2";
    reg20->SetTextValue("-2");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), reg20->GetTextValue());
    EXPECT_EQ(0xFFFF, Device->Registers[20]);
    EXPECT_EQ(0xFFFF, Device->Registers[21]);
    EXPECT_EQ(0xFFFF, Device->Registers[22]);
    EXPECT_EQ(0xFFFE, Device->Registers[23]);
}

TEST_F(TSerialClientTest, U64)
{
    auto reg20 = Reg(20, U64);
    auto reg30 = Reg(30, U64);
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
    EXPECT_EQ(to_string(0x00AA00BB00CC00DD), reg20->GetTextValue());
    EXPECT_EQ("18446744073709551615", reg30->GetTextValue());

    Note() << "client -> server: 10";
    reg20->SetTextValue("10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), reg20->GetTextValue());
    EXPECT_EQ(0, Device->Registers[20]);
    EXPECT_EQ(0, Device->Registers[21]);
    EXPECT_EQ(0, Device->Registers[22]);
    EXPECT_EQ(10, Device->Registers[23]);

    Note() << "client -> server: -2";
    reg20->SetTextValue("-2");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("18446744073709551614", reg20->GetTextValue());
    EXPECT_EQ(0xFFFF, Device->Registers[20]);
    EXPECT_EQ(0xFFFF, Device->Registers[21]);
    EXPECT_EQ(0xFFFF, Device->Registers[22]);
    EXPECT_EQ(0xFFFE, Device->Registers[23]);
}

TEST_F(TSerialClientTest, S32)
{
    auto reg20 = Reg(20, S32);
    auto reg30 = Reg(30, S32);
    SerialClient->AddRegister(reg20);
    SerialClient->AddRegister(reg30);

    // create scaled register
    auto reg24 = Reg(24, S32, 0.001);
    SerialClient->AddRegister(reg24);

    Note() << "server -> client: 10, 20";
    Device->Registers[20] = 0x00AA;
    Device->Registers[21] = 0x00BB;
    Device->Registers[30] = 0xFFFF;
    Device->Registers[31] = 0xFFFF;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB), reg20->GetTextValue());
    EXPECT_EQ(to_string(-1), reg30->GetTextValue());

    Note() << "client -> server: 10";
    reg20->SetTextValue("10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), reg20->GetTextValue());
    EXPECT_EQ(0, Device->Registers[20]);
    EXPECT_EQ(10, Device->Registers[21]);

    Note() << "client -> server: -2";
    reg20->SetTextValue("-2");
    EXPECT_EQ(to_string(-2), reg20->GetTextValue());
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), reg20->GetTextValue());
    EXPECT_EQ(0xFFFF, Device->Registers[20]);
    EXPECT_EQ(0xFFFE, Device->Registers[21]);

    Note() << "client -> server: -0.123 (scaled)";
    reg24->SetTextValue("-0.123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", reg24->GetTextValue());
    EXPECT_EQ(0xffff, Device->Registers[24]);
    EXPECT_EQ(0xff85, Device->Registers[25]);

    Note() << "server -> client: 0xffff 0xff85 (scaled)";
    Device->Registers[24] = 0xffff;
    Device->Registers[25] = 0xff85;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", reg24->GetTextValue());
}

TEST_F(TSerialClientTest, WordSwap)
{
    auto reg20 = Reg(20, S32, 1, 0, 0, EWordOrder::LittleEndian);
    auto reg24 = Reg(24, U64, 1, 0, 0, EWordOrder::LittleEndian);
    SerialClient->AddRegister(reg24);
    SerialClient->AddRegister(reg20);

    Note() << "server -> client: 0x00BB, 0x00AA";
    Device->Registers[20] = 0x00BB;
    Device->Registers[21] = 0x00AA;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB), reg20->GetTextValue());

    Note() << "client -> server: -2";
    reg20->SetTextValue("-2");
    EXPECT_EQ(to_string(-2), reg20->GetTextValue());
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), reg20->GetTextValue());
    EXPECT_EQ(0xFFFE, Device->Registers[20]);
    EXPECT_EQ(0xFFFF, Device->Registers[21]);

    // U64
    Note() << "client -> server: 10";
    reg24->SetTextValue("47851549213065437"); // 0x00AA00BB00CC00DD
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB00CC00DD), reg24->GetTextValue());
    EXPECT_EQ(0x00DD, Device->Registers[24]);
    EXPECT_EQ(0x00CC, Device->Registers[25]);
    EXPECT_EQ(0x00BB, Device->Registers[26]);
    EXPECT_EQ(0x00AA, Device->Registers[27]);

}

TEST_F(TSerialClientTest, U32)
{
    auto reg20 = Reg(20, U32);
    auto reg30 = Reg(30, U32);
    SerialClient->AddRegister(reg20);
    SerialClient->AddRegister(reg30);

    Note() << "server -> client: 10, 20";
    Device->Registers[20] = 0x00AA;
    Device->Registers[21] = 0x00BB;
    Device->Registers[30] = 0xFFFF;
    Device->Registers[31] = 0xFFFF;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB), reg20->GetTextValue());
    EXPECT_EQ(to_string(0xFFFFFFFF), reg30->GetTextValue());

    Note() << "client -> server: 10";
    reg20->SetTextValue("10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), reg20->GetTextValue());
    EXPECT_EQ(0, Device->Registers[20]);
    EXPECT_EQ(10, Device->Registers[21]);

    Note() << "client -> server: -1 (overflow)";
    reg20->SetTextValue("-1");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0xFFFFFFFF), reg20->GetTextValue());
    EXPECT_EQ(0xFFFF, Device->Registers[20]);
    EXPECT_EQ(0xFFFF, Device->Registers[21]);

    Device->Registers[22] = 123;
    Device->Registers[23] = 123;
    Note() << "client -> server: 4294967296 (overflow)";
    reg20->SetTextValue("4294967296");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0), reg20->GetTextValue());
    EXPECT_EQ(0, Device->Registers[20]);
    EXPECT_EQ(0, Device->Registers[21]);

    //boundaries check
    EXPECT_EQ(123, Device->Registers[22]);
    EXPECT_EQ(123, Device->Registers[23]);
}

TEST_F(TSerialClientTest, BCD32)
{
    auto reg20 = Reg(20, BCD32);
    SerialClient->AddRegister(reg20);
    Device->Registers[22] = 123;
    Device->Registers[23] = 123;

    Note() << "server -> client: 0x1234 0x5678";
    Device->Registers[20] = 0x1234;
    Device->Registers[21] = 0x5678;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(12345678), reg20->GetTextValue());

    Note() << "client -> server: 12345678";
    reg20->SetTextValue("12345678");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(12345678), reg20->GetTextValue());
    EXPECT_EQ(0x1234, Device->Registers[20]);
    EXPECT_EQ(0x5678, Device->Registers[21]);

    Note() << "client -> server: 567890";
    reg20->SetTextValue("567890");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(567890), reg20->GetTextValue());
    EXPECT_EQ(0x0056, Device->Registers[20]);
    EXPECT_EQ(0x7890, Device->Registers[21]);

    //boundaries check
    EXPECT_EQ(123, Device->Registers[22]);
    EXPECT_EQ(123, Device->Registers[23]);
}

TEST_F(TSerialClientTest, BCD24)
{
    auto reg20 = Reg(20, BCD24);
    SerialClient->AddRegister(reg20);
    Device->Registers[22] = 123;
    Device->Registers[23] = 123;

    Note() << "server -> client: 0x0034 0x5678";
    Device->Registers[20] = 0x0034;
    Device->Registers[21] = 0x5678;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(345678), reg20->GetTextValue());

    Note() << "client -> server: 567890";
    reg20->SetTextValue("567890");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(567890), reg20->GetTextValue());
    EXPECT_EQ(0x0056, Device->Registers[20]);
    EXPECT_EQ(0x7890, Device->Registers[21]);

    //boundaries check
    EXPECT_EQ(123, Device->Registers[22]);
    EXPECT_EQ(123, Device->Registers[23]);
}

TEST_F(TSerialClientTest, BCD16)
{
    auto reg20 = Reg(20, BCD16);
    SerialClient->AddRegister(reg20);
    Device->Registers[21] = 123;

    Note() << "server -> client: 0x1234";
    Device->Registers[20] = 0x1234;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(1234), reg20->GetTextValue());

    Note() << "client -> server: 1234";
    reg20->SetTextValue("1234");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(1234), reg20->GetTextValue());
    EXPECT_EQ(0x1234, Device->Registers[20]);

    //boundaries check
    EXPECT_EQ(123, Device->Registers[21]);
}

TEST_F(TSerialClientTest, BCD8)
{
    auto reg20 = Reg(20, BCD8);
    SerialClient->AddRegister(reg20);
    Device->Registers[21] = 123;

    Note() << "server -> client: 0x12";
    Device->Registers[20] = 0x12;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(12), reg20->GetTextValue());

    Note() << "client -> server: 12";
    reg20->SetTextValue("12");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(12), reg20->GetTextValue());
    EXPECT_EQ(0x12, Device->Registers[20]);

    //boundaries check
    EXPECT_EQ(123, Device->Registers[21]);
}

TEST_F(TSerialClientTest, Float32)
{
	// create scaled register
    auto reg24 = Reg(24, Float, 100);
    SerialClient->AddRegister(reg24);

    auto reg20 = Reg(20, Float);
    auto reg30 = Reg(30, Float);
    SerialClient->AddRegister(reg20);
    SerialClient->AddRegister(reg30);

    Note() << "server -> client: 0x45d2 0x0000, 0x449d 0x8000";
    Device->Registers[20] = 0x45d2;
    Device->Registers[21] = 0x0000;
    Device->Registers[30] = 0x449d;
    Device->Registers[31] = 0x8000;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("6720", reg20->GetTextValue());
    EXPECT_EQ("1260", reg30->GetTextValue());

    Note() << "client -> server: 10";
    reg20->SetTextValue("10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("10", reg20->GetTextValue());
    EXPECT_EQ(0x4120, Device->Registers[20]);
    EXPECT_EQ(0x0000, Device->Registers[21]);

    Note() << "client -> server: -0.00123";
    reg20->SetTextValue("-0.00123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.00123", reg20->GetTextValue());
    EXPECT_EQ(0xbaa1, Device->Registers[20]);
    EXPECT_EQ(0x37f4, Device->Registers[21]);

    Note() << "client -> server: -0.123 (scaled)";
    reg24->SetTextValue("-0.123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", reg24->GetTextValue());
    EXPECT_EQ(0xbaa1, Device->Registers[24]);
    EXPECT_EQ(0x37f4, Device->Registers[25]);

    Note() << "server -> client: 0x449d 0x8000 (scaled)";
    Device->Registers[24] = 0x449d;
    Device->Registers[25] = 0x8000;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("126000", reg24->GetTextValue());
}

TEST_F(TSerialClientTest, Double64)
{
	// create scaled register
    auto reg24 = Reg(24, Double, 100);
    SerialClient->AddRegister(reg24);

    auto reg20 = Reg(20, Double);
    auto reg30 = Reg(30, Double);
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
    EXPECT_EQ("6720.123", reg20->GetTextValue());
    EXPECT_EQ("1260.321", reg30->GetTextValue());

    Note() << "client -> server: 10";
    reg20->SetTextValue("10.9999");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("10.9999", reg20->GetTextValue());
    EXPECT_EQ(0x4025, Device->Registers[20]);
    EXPECT_EQ(0xfff2, Device->Registers[21]);
    EXPECT_EQ(0xe48e, Device->Registers[22]);
    EXPECT_EQ(0x8a72, Device->Registers[23]);

    Note() << "client -> server: -0.00123";
    reg20->SetTextValue("-0.00123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.00123", reg20->GetTextValue());
    EXPECT_EQ(0xbf54, Device->Registers[20]);
    EXPECT_EQ(0x26fe, Device->Registers[21]);
    EXPECT_EQ(0x718a, Device->Registers[22]);
    EXPECT_EQ(0x86d7, Device->Registers[23]);

    Note() << "client -> server: -0.123 (scaled)";
    reg24->SetTextValue("-0.123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", reg24->GetTextValue());
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
    EXPECT_EQ("126000", reg24->GetTextValue());
}

TEST_F(TSerialClientTest, offset)
{
    // create scaled register with offset
    auto reg24 = Reg(24, S16, 3, -15);
    SerialClient->AddRegister(reg24);

    Note() << "client -> server: -87 (scaled)";
    reg24->SetTextValue("-87");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-87", reg24->GetTextValue());
    EXPECT_EQ(0xffe8, Device->Registers[24]);
}

TEST_F(TSerialClientTest, Round)
{
    auto reg24_0_01 = Reg(24, Float, 1, 0, 0.01);
    auto reg26_1 = Reg(26, Float, 1, 0, 1);
    auto reg28_10 = Reg(28, Float, 1, 0, 10);
    auto reg30_0_2 = Reg(30, Float, 1, 0, 0.2);
    auto reg32_0_01 = Reg(32, Float, 1, 0, 0.01);

    SerialClient->AddRegister(reg24_0_01);
    SerialClient->AddRegister(reg26_1);
    SerialClient->AddRegister(reg28_10);
    SerialClient->AddRegister(reg30_0_2);
    SerialClient->AddRegister(reg32_0_01);

    Note() << "client -> server: 12.345 (not rounded)";
    reg24_0_01->SetTextValue("12.345");
    reg26_1->SetTextValue("12.345");
    reg28_10->SetTextValue("12.345");
    reg30_0_2->SetTextValue("12.345");
    reg32_0_01->SetTextValue("12.344");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("12.35", reg24_0_01->GetTextValue());
    EXPECT_EQ("12", reg26_1->GetTextValue());
    EXPECT_EQ("10", reg28_10->GetTextValue());
    EXPECT_EQ("12.4", reg30_0_2->GetTextValue());
    EXPECT_EQ("12.34", reg32_0_01->GetTextValue());

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
    auto reg20 = Reg(20);
    Device->BlockReadFor(20, true);
    Device->BlockWriteFor(20, true);
    SerialClient->AddRegister(reg20);

    for (int i = 0; i < 3; i++) {
        Note() << "Cycle() [read, rw blacklisted]";
        SerialClient->Cycle();
    }

    reg20->SetTextValue("42");
    Note() << "Cycle() [write, rw blacklisted]";
    SerialClient->Cycle();

    Device->BlockWriteFor(20, false);
    reg20->SetTextValue("42");
    Note() << "Cycle() [write, r blacklisted]";
    SerialClient->Cycle();

    Device->BlockWriteFor(20, true);
    reg20->SetTextValue("43");
    Note() << "Cycle() [write, rw blacklisted]";
    SerialClient->Cycle();

    Device->BlockReadFor(20, false);
    Note() << "Cycle() [write, w blacklisted]";
    SerialClient->Cycle();

    Device->BlockWriteFor(20, false);
    reg20->SetTextValue("42");
    Note() << "Cycle() [write, nothing blacklisted]";
    SerialClient->Cycle();


    reg20->HasErrorValue = true;
    reg20->ErrorValue = 42;
    Note() << "Cycle() [read, set error value for register]";
    SerialClient->Cycle();
    reg20->GetTextValue();

    SerialClient->Cycle();
}


class TSerialClientIntegrationTest: public TSerialClientTest
{
protected:
    void SetUp();
    void FilterConfig(const std::string& device_name);

    /** reconnect test functions **/
    static void DeviceTimeoutOnly(const PSerialDevice & device, int timeout);
    static void DeviceMaxFailCyclesOnly(const PSerialDevice & device, int cycleCount);
    static void DeviceTimeoutAndMaxFailCycles(const PSerialDevice & device, int timeout, int cycleCount);

    PMQTTSerialObserver StartReconnectTest1Device(bool miss = false, bool pollIntervalTest = false);
    PMQTTSerialObserver StartReconnectTest2Devices();

    void ReconnectTest1Device(function<void()> && thunk, bool pollIntervalTest = false);
    void ReconnectTest2Devices(function<void()> && thunk);

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

void TSerialClientIntegrationTest::DeviceTimeoutOnly(const PSerialDevice & device, int timeout)
{
    device->DeviceConfig()->DeviceTimeout = chrono::milliseconds(timeout);
    device->DeviceConfig()->DeviceMaxFailCycles = 0;
}

void TSerialClientIntegrationTest::DeviceMaxFailCyclesOnly(const PSerialDevice & device, int cycleCount)
{
    device->DeviceConfig()->DeviceTimeout = chrono::milliseconds(0);
    device->DeviceConfig()->DeviceMaxFailCycles = cycleCount;
}

void TSerialClientIntegrationTest::DeviceTimeoutAndMaxFailCycles(const PSerialDevice & device, int timeout, int cycleCount)
{
    device->DeviceConfig()->DeviceTimeout = chrono::milliseconds(0);
    device->DeviceConfig()->DeviceMaxFailCycles = cycleCount;
}

TEST_F(TSerialClientIntegrationTest, OnValue)
{
    FilterConfig("OnValueTest");

    PMQTTSerialObserver observer(new TMQTTSerialObserver(MQTTClient, Config, Port));
    observer->SetUp();

    Device = std::dynamic_pointer_cast<TFakeSerialDevice>(TSerialDeviceFactory::GetDevice("0x90", "fake", Port));

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

    Device = std::dynamic_pointer_cast<TFakeSerialDevice>(TSerialDeviceFactory::GetDevice("0x91", "fake", Port));

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

    Device = std::dynamic_pointer_cast<TFakeSerialDevice>(TSerialDeviceFactory::GetDevice("0x92", "fake", Port));

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

    Device = std::dynamic_pointer_cast<TFakeSerialDevice>(TSerialDeviceFactory::GetDevice("23", "fake", Port));

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

TEST_F(TSerialClientIntegrationTest, SlaveIdCollision)
{
    {
        TConfigParser parser(GetDataFilePath("configs/config-collision-test.json"), false, TSerialDeviceFactory::GetRegisterTypes);
        Config = parser.Parse();
        EXPECT_THROW(make_shared<TMQTTSerialObserver>(MQTTClient, Config), TSerialDeviceException);
    }

    {
        TConfigParser parser(GetDataFilePath("configs/config-no-collision-test.json"), false, TSerialDeviceFactory::GetRegisterTypes);
        Config = parser.Parse();
        EXPECT_NO_THROW(make_shared<TMQTTSerialObserver>(MQTTClient, Config));
    }
}

/** Reconnect test cases **/
/*
    Test configurations:
    1) 1 device; 1 poll interval for all registers;
    2) 1 device; one long poll interval for one registers, one short poll interval for the rest of registers;
    3) 2 devices; 1 poll interval for all registers; only 1 device disconnect - connect

    Test conditions:
    1) device_timeout_ms only
    2) device_fail_cycles only
    3) device_timeout_ms and device_fail_cycles
*/

PMQTTSerialObserver TSerialClientIntegrationTest::StartReconnectTest1Device(bool miss, bool pollIntervalTest)
{
    TConfigParser parser(GetDataFilePath("configs/reconnect_test_1_device.json"), false,
                         TSerialDeviceFactory::GetRegisterTypes);
    Config = parser.Parse();

    if (pollIntervalTest) {
        Config->PortConfigs[0]->DeviceConfigs[0]->DeviceChannelConfigs[0]->RegisterConfigs[0]->PollInterval = chrono::seconds(100);
    }

    PMQTTSerialObserver observer(new TMQTTSerialObserver(MQTTClient, Config, Port));

    observer->SetUp();

    Device = std::dynamic_pointer_cast<TFakeSerialDevice>(TSerialDeviceFactory::GetDevice("12", "fake", Port));

    {   // Test initial WriteInitValues
        observer->WriteInitValues();

        EXPECT_EQ(42, Device->Registers[1]);
        EXPECT_EQ(24, Device->Registers[2]);
    }

    {   // Test read
        Note() << "LoopOnce()";
        observer->LoopOnce();
    }

    {   // Device disconnected
        Note() << "SimulateDisconnect(true)";
        Device->SetIsConnected(false);

        {
            bool by_timeout = Device->DeviceConfig()->DeviceTimeout.count() > 0;
            bool by_cycles = Device->DeviceConfig()->DeviceMaxFailCycles > 0;

            if (by_timeout) {
                auto delay = Device->DeviceConfig()->DeviceTimeout;

                if (miss) {
                    delay -= chrono::milliseconds(100);
                }

                if (by_cycles) {
                    auto disconnectTimepoint = std::chrono::steady_clock::now();

                    // Couple of unsuccessful reads
                    while (std::chrono::steady_clock::now() - disconnectTimepoint < delay) {
                        Note() << "LoopOnce()";
                        observer->LoopOnce();
                        usleep(std::chrono::duration_cast<std::chrono::microseconds>(delay).count() / Device->DeviceConfig()->DeviceMaxFailCycles);
                    }
                } else {
                    auto disconnectTimepoint = std::chrono::steady_clock::now();

                    // Couple of unsuccessful reads
                    while (std::chrono::steady_clock::now() - disconnectTimepoint < delay) {
                        Note() << "LoopOnce()";
                        observer->LoopOnce();
                        usleep(std::chrono::duration_cast<std::chrono::microseconds>(delay).count() / 10);
                    }
                }
            } else if (by_cycles) {
                // Couple of unsuccessful reads
                auto remainingCycles = Device->DeviceConfig()->DeviceMaxFailCycles;

                if (miss) {
                    --remainingCycles;
                }

                while (remainingCycles--) {
                    Note() << "LoopOnce()";
                    observer->LoopOnce();
                }
            } else {
                auto disconnectTimepoint = std::chrono::steady_clock::now();
                auto delay = chrono::milliseconds(10000);

                // Couple of unsuccessful reads
                while (std::chrono::steady_clock::now() - disconnectTimepoint < delay) {
                    Note() << "LoopOnce()";
                    observer->LoopOnce();
                    usleep(std::chrono::duration_cast<std::chrono::microseconds>(delay).count() / 10);
                }
            }
        }



        // Final unsuccessful read after timeout, after this loop we expect device to be counted as disconnected
        Note() << "LoopOnce()";
        observer->LoopOnce();
    }

    return observer;
}

PMQTTSerialObserver TSerialClientIntegrationTest::StartReconnectTest2Devices()
{
    TConfigParser parser(GetDataFilePath("configs/reconnect_test_2_devices.json"), false,
                         TSerialDeviceFactory::GetRegisterTypes);
    Config = parser.Parse();

    PMQTTSerialObserver observer(new TMQTTSerialObserver(MQTTClient, Config, Port));

    observer->SetUp();

    auto dev1 = std::dynamic_pointer_cast<TFakeSerialDevice>(TSerialDeviceFactory::GetDevice("12", "fake", Port));
    auto dev2 = std::dynamic_pointer_cast<TFakeSerialDevice>(TSerialDeviceFactory::GetDevice("13", "fake", Port));

    {   // Test initial WriteInitValues
        observer->WriteInitValues();

        EXPECT_EQ(42, dev1->Registers[1]);
        EXPECT_EQ(24, dev1->Registers[2]);

        EXPECT_EQ(32, dev2->Registers[1]);
        EXPECT_EQ(64, dev2->Registers[2]);
    }

    {   // Test read
        Note() << "LoopOnce()";
        observer->LoopOnce();
    }

    {   // Device disconnected
        Note() << "SimulateDisconnect(true)";
        dev1->SetIsConnected(false);

        bool by_timeout = Device->DeviceConfig()->DeviceTimeout.count() > 0;
        bool by_cycles = Device->DeviceConfig()->DeviceMaxFailCycles > 0;

        if (by_timeout) {
            if (by_cycles) {
                auto disconnectTimepoint = std::chrono::steady_clock::now();

                // Couple of unsuccessful reads
                while (std::chrono::steady_clock::now() - disconnectTimepoint < Device->DeviceConfig()->DeviceTimeout) {
                    Note() << "LoopOnce()";
                    observer->LoopOnce();
                    usleep(std::chrono::duration_cast<std::chrono::microseconds>(Device->DeviceConfig()->DeviceTimeout).count() / Device->DeviceConfig()->DeviceMaxFailCycles);
                }
            } else {
                auto disconnectTimepoint = std::chrono::steady_clock::now();

                // Couple of unsuccessful reads
                while (std::chrono::steady_clock::now() - disconnectTimepoint < Device->DeviceConfig()->DeviceTimeout) {
                    Note() << "LoopOnce()";
                    observer->LoopOnce();
                    usleep(std::chrono::duration_cast<std::chrono::microseconds>(Device->DeviceConfig()->DeviceTimeout).count() / 10);
                }
            }
        } else if (by_cycles) {
            // Couple of unsuccessful reads
            auto remainingCycles = Device->DeviceConfig()->DeviceMaxFailCycles;

            while (remainingCycles--) {
                Note() << "LoopOnce()";
                observer->LoopOnce();
            }
        } else {
            auto disconnectTimepoint = std::chrono::steady_clock::now();
            auto delay = chrono::milliseconds(10000);

            // Couple of unsuccessful reads
            while (std::chrono::steady_clock::now() - disconnectTimepoint < delay) {
                Note() << "LoopOnce()";
                observer->LoopOnce();
                usleep(std::chrono::duration_cast<std::chrono::microseconds>(delay).count() / 10);
            }
        }

        // Final unsuccessful read after timeout, after this loop we expect device to be counted as disconnected
        Note() << "LoopOnce()";
        observer->LoopOnce();
    }

    return observer;
}

void TSerialClientIntegrationTest::ReconnectTest1Device(function<void()> && thunk, bool pollIntervalTest)
{
    auto observer = StartReconnectTest1Device(false, pollIntervalTest);

    Device->Registers[1] = 0;   // reset those to make sure that setup section is wrote again
    Device->Registers[2] = 0;

    thunk();

    {   // Loop to check limited polling
        Note() << "LoopOnce() (limited polling expected)";
        observer->LoopOnce();
    }

    {   // Device is connected back
        Note() << "SimulateDisconnect(false)";
        Device->SetIsConnected(true);

        Note() << "LoopOnce()";
        observer->LoopOnce();

        EXPECT_EQ(42, Device->Registers[1]);
        EXPECT_EQ(24, Device->Registers[2]);
    }
}

void TSerialClientIntegrationTest::ReconnectTest2Devices(function<void()> && thunk)
{
    auto observer = StartReconnectTest2Devices();

    auto dev1 = std::dynamic_pointer_cast<TFakeSerialDevice>(TSerialDeviceFactory::GetDevice("12", "fake", Port));
    auto dev2 = std::dynamic_pointer_cast<TFakeSerialDevice>(TSerialDeviceFactory::GetDevice("13", "fake", Port));

    dev1->Registers[1] = 0;   // reset those to make sure that setup section is wrote again
    dev1->Registers[2] = 0;

    dev2->Registers[1] = 1;   // set control values to make sure that no setup section is written
    dev2->Registers[2] = 2;

    thunk();

    {   // Loop to check limited polling
        Note() << "LoopOnce() (limited polling expected)";
        observer->LoopOnce();
    }

    {   // Device is connected back
        Note() << "SimulateDisconnect(false)";
        dev1->SetIsConnected(true);

        Note() << "LoopOnce()";
        observer->LoopOnce();

        EXPECT_EQ(42, dev1->Registers[1]);
        EXPECT_EQ(24, dev1->Registers[2]);

        EXPECT_EQ(1, dev2->Registers[1]);
        EXPECT_EQ(2, dev2->Registers[2]);
    }
}

TEST_F(TSerialClientIntegrationTest, ReconnectTimeout)
{
    ReconnectTest1Device([&]{
        DeviceTimeoutOnly(Device, DEFAULT_DEVICE_TIMEOUT_MS);
    });
}

TEST_F(TSerialClientIntegrationTest, ReconnectCycles)
{
    ReconnectTest1Device([&]{
        DeviceMaxFailCyclesOnly(Device, 10);
    });
}

TEST_F(TSerialClientIntegrationTest, ReconnectTimeoutAndCycles)
{
    ReconnectTest1Device([&]{
        DeviceTimeoutAndMaxFailCycles(Device, DEFAULT_DEVICE_TIMEOUT_MS, 10);
    });
}

TEST_F(TSerialClientIntegrationTest, ReconnectRegisterWithBigPollInterval)
{
    auto t1 = chrono::steady_clock::now();

    ReconnectTest1Device([&]{
        DeviceTimeoutOnly(Device, DEFAULT_DEVICE_TIMEOUT_MS);
    }, true);

    auto time = chrono::steady_clock::now() - t1;

    EXPECT_LT(time, chrono::seconds(100));
}

TEST_F(TSerialClientIntegrationTest, Reconnect2)
{
    ReconnectTest2Devices([&]{
        DeviceTimeoutOnly(Device, DEFAULT_DEVICE_TIMEOUT_MS);
    });
}

TEST_F(TSerialClientIntegrationTest, ReconnectMiss)
{
    Device = std::dynamic_pointer_cast<TFakeSerialDevice>(TSerialDeviceFactory::GetDevice("1", "fake", Port));

    auto observer = StartReconnectTest1Device(true);

    Device->Registers[1] = 1;   // set control values to make sure that no setup section is written
    Device->Registers[2] = 2;

    {   // Device is connected back before timeout
        Note() << "SimulateDisconnect(false)";
        Device->SetIsConnected(true);

        Note() << "LoopOnce()";
        observer->LoopOnce();

        EXPECT_EQ(1, Device->Registers[1]);
        EXPECT_EQ(2, Device->Registers[2]);
    }
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
        Emit() << "GuardInterval: " << port_config->GuardInterval.count();

        if(auto tcp_port_config = dynamic_pointer_cast<TTcpPortSettings>(port_config->ConnSettings)) {
            Emit() << "ConnectionTimeout: " << tcp_port_config->ConnectionTimeout.count();
        }

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
            Emit() << "DeviceTimeout: " << device_config->DeviceTimeout.count();
            if (!device_config->DeviceChannelConfigs.empty()) {
                Emit() << "DeviceChannels:";
                for (auto device_channel: device_config->DeviceChannelConfigs) {
                    TTestLogIndent indent(*this);
                    Emit() << "------";
                    Emit() << "Name: " << device_channel->Name;
                    Emit() << "Type: " << device_channel->Type;
                    Emit() << "DeviceId: " << device_channel->DeviceId;
                    Emit() << "Order: " << device_channel->Order;
                    Emit() << "Max: " << device_channel->Max;
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
                        Emit() << "OnValue: " << reg->OnValue;
                        Emit() << "Poll: " << reg->Poll;
                        Emit() << "ReadOnly: " << reg->ReadOnly;
                        Emit() << "TypeName: " << reg->TypeName;
                        Emit() << "PollInterval: " << reg->PollInterval.count();
                        if (reg->HasErrorValue) {
                            Emit() << "ErrorValue: " << reg->ErrorValue;
                        } else {
                            Emit() << "ErrorValue: not set";
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
    TConfigParser parser(GetDataFilePath("configs/config-test.json"), true,
                         TSerialDeviceFactory::GetRegisterTypes);
    PHandlerConfig config = parser.Parse();
    ASSERT_TRUE(config->Debug);
}

TEST_F(TConfigParserTest, UnsuccessfulParse)
{
    for (size_t i = 0; i < 4; ++i) {
        auto fname = std::string("configs/unsuccessful/unsuccessful-") + to_string(i) +  ".json";
        Emit() << "Parsing config " << fname;
        TConfigParser parser(GetDataFilePath(fname), true,
                            TSerialDeviceFactory::GetRegisterTypes);
        try {
            PHandlerConfig config = parser.Parse();
        } catch (const std::exception& e) {
            Emit() << e.what();
        }
    }

}
// TBD: the code must check mosquitto return values
