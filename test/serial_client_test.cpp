#include "fake_serial_device.h"
#include "fake_serial_port.h"
#include "log.h"
#include "rpc/rpc_port_handler.h"
#include "rpc/rpc_port_load_raw_serial_client_task.h"
#include "serial_driver.h"

#include <wblib/driver_args.h>
#include <wblib/testing/fake_driver.h>
#include <wblib/testing/fake_mqtt.h>

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <string>

#include "tcp_port_settings.h"

using namespace std;
using namespace WBMQTT;
using namespace WBMQTT::Testing;

#define LOG(logger) ::logger.Log() << "[serial client test] "

namespace
{
    const auto DB_PATH = "/tmp/wb-mqtt-serial-test.db";

    std::string GetTextValue(PRegister reg)
    {
        return ConvertFromRawValue(*reg->GetConfig(), reg->GetValue());
    }
}

class TSerialClientTest: public TLoggedFixture
{
    std::unordered_map<PRegister, std::string> LastRegValues;
    std::unordered_map<PRegister, TRegister::TErrorState> LastRegErrors;

    void EmitErrorMsg(PRegister reg)
    {
        if (LastRegErrors.count(reg) && LastRegErrors[reg] == reg->GetErrorState()) {
            return;
        }
        std::string what;
        const std::vector<std::string> errorNames = {"read", "write", "poll interval miss"};
        for (size_t i = 0; i < TRegister::TError::MAX_ERRORS; ++i) {
            if (reg->GetErrorState().test(i)) {
                if (!what.empty()) {
                    what += "+";
                }
                if (i < errorNames.size()) {
                    what += errorNames[i];
                } else {
                    what += "unknown";
                }
            }
        }
        if (what.empty()) {
            what = "no";
        }
        Emit() << "Error Callback: <" << reg->Device()->ToString() << ":" << reg->GetConfig()->TypeName << ": "
               << reg->GetConfig()->GetAddress() << ">: " << what << " error";
        LastRegErrors[reg] = reg->GetErrorState();
    }

protected:
    TSerialClientTest()
    {
        PortOpenCloseSettings.ReopenTimeout = std::chrono::milliseconds(0);
    }

    void SetUp();
    void TearDown();
    PRegister Reg(int addr,
                  RegisterFormat fmt = U16,
                  double scale = 1,
                  double offset = 0,
                  double round_to = 0,
                  EWordOrder word_order = EWordOrder::BigEndian,
                  EByteOrder byte_order = EByteOrder::BigEndian,
                  uint32_t dataOffset = 0,
                  uint32_t dataBitWidth = 0)
    {
        return Device->AddRegister(TRegisterConfig::Create(TFakeSerialDevice::REG_FAKE,
                                                           addr,
                                                           fmt,
                                                           scale,
                                                           offset,
                                                           round_to,
                                                           TRegisterConfig::TSporadicMode::DISABLED,
                                                           false,
                                                           "fake",
                                                           word_order,
                                                           byte_order,
                                                           dataOffset,
                                                           dataBitWidth));
    }
    PFakeSerialPort Port;
    PSerialClient SerialClient;
    PFakeSerialDevice Device;
    TSerialDeviceFactory DeviceFactory;
    PRPCConfig rpcConfig;

    bool HasSetupRegisters = false;
    TPortOpenCloseLogic::TSettings PortOpenCloseSettings;
};

class TSerialClientReopenTest: public TSerialClientTest
{
public:
    TSerialClientReopenTest()
    {
        PortOpenCloseSettings.ReopenTimeout = std::chrono::milliseconds(700);
    }
};

class TSerialClientTestWithSetupRegisters: public TSerialClientTest
{
public:
    TSerialClientTestWithSetupRegisters()
    {
        HasSetupRegisters = true;
    }
};

void TSerialClientTest::SetUp()
{
    RegisterProtocols(DeviceFactory);
    TFakeSerialDevice::Register(DeviceFactory);

    TLoggedFixture::SetUp();
    Port = std::make_shared<TFakeSerialPort>(*this, "<TSerialClientTest>");
    auto config = std::make_shared<TDeviceConfig>("fake_sample", "1", "fake");
    config->MaxReadRegisters = 0;

    config->FrameTimeout = std::chrono::milliseconds(100);
    Device = std::make_shared<TFakeSerialDevice>(config, DeviceFactory.GetProtocol("fake"));
    if (HasSetupRegisters) {
        PRegisterConfig reg1 = TRegisterConfig::Create(TFakeSerialDevice::REG_FAKE, 100);
        Device->AddSetupItem(PDeviceSetupItemConfig(new TDeviceSetupItemConfig("setup1", reg1, "10")));

        PRegisterConfig reg2 = TRegisterConfig::Create(TFakeSerialDevice::REG_FAKE, 101);
        Device->AddSetupItem(PDeviceSetupItemConfig(new TDeviceSetupItemConfig("setup2", reg2, "11")));

        PRegisterConfig reg3 = TRegisterConfig::Create(TFakeSerialDevice::REG_FAKE, 102);
        Device->AddSetupItem(PDeviceSetupItemConfig(new TDeviceSetupItemConfig("setup3", reg3, "12")));
    }
    Device->SetFakePort(Port);
    SerialClient = std::make_shared<TSerialClient>(Port, PortOpenCloseSettings, std::chrono::steady_clock::now, 8ms);
    SerialClient->SetReadCallback([this](PRegister reg) {
        if (reg->GetErrorState().count()) {
            EmitErrorMsg(reg);
        }
        std::string value = GetTextValue(reg);
        bool unchanged = (LastRegValues.count(reg) && LastRegValues[reg] == value);
        Emit() << "Read Callback: <" << reg->Device()->ToString() << ":" << reg->GetConfig()->TypeName << ": "
               << reg->GetConfig()->GetAddress() << "> becomes " << value << (unchanged ? " [unchanged]" : "");
        LastRegValues[reg] = value;
        if (!reg->GetErrorState().count()) {
            EmitErrorMsg(reg);
        }
    });
    SerialClient->SetErrorCallback([this](PRegister reg) { EmitErrorMsg(reg); });
}

void TSerialClientTest::TearDown()
{
    TLoggedFixture::TearDown();
    TFakeSerialDevice::ClearDevices();
}

TEST_F(TSerialClientTest, PortOpenError)
{
    // The test checks recovery logic after port opening failure
    // TSerialClient must try to open port during every cycle and set /meta/error for controls

    PRegister reg0 = Reg(0, U8);
    PRegister reg1 = Reg(1, U8);

    SerialClient->AddDevice(Device);

    Port->SetAllowOpen(false);

    Note() << "Cycle() [port open error]";
    SerialClient->Cycle();

    Note() << "Cycle() [port open error2]";
    SerialClient->Cycle();

    Port->SetAllowOpen(true);

    Note() << "Cycle() [successful port open]";
    SerialClient->Cycle();
}

TEST_F(TSerialClientReopenTest, ReopenTimeout)
{
    // The test checks recovery logic after port opening failure
    // TSerialClient must try to open port during every cycle and set /meta/error for controls

    PRegister reg0 = Reg(0, U8);
    PRegister reg1 = Reg(1, U8);

    SerialClient->AddDevice(Device);

    Port->SetAllowOpen(false);

    Note() << "Cycle() [port open error]";
    SerialClient->Cycle();

    Port->SetAllowOpen(true);

    // The opening will be unsuccessful because of 700ms timeout since last failed open
    Note() << "Cycle() [port open error2]";
    SerialClient->Cycle();

    std::this_thread::sleep_for(PortOpenCloseSettings.ReopenTimeout);

    Note() << "Cycle() [successful port open]";
    SerialClient->Cycle();
}

TEST_F(TSerialClientTest, Poll)
{

    PRegister reg0 = Reg(0, U8);
    PRegister reg1 = Reg(1, U8);
    PRegister discrete10 = Reg(10);
    PRegister reg22 = Reg(22);
    PRegister reg33 = Reg(33);

    SerialClient->AddDevice(Device);

    Note() << "Cycle()";
    SerialClient->Cycle();

    Device->Registers[1] = 1;
    Device->Registers[10] = 1;
    Device->Registers[22] = 4242;
    Device->Registers[33] = 42000;

    Note() << "Cycle()";
    SerialClient->Cycle();

    EXPECT_EQ(to_string(0), GetTextValue(reg0));
    EXPECT_EQ(to_string(1), GetTextValue(reg1));
    EXPECT_EQ(to_string(1), GetTextValue(discrete10));
    EXPECT_EQ(to_string(4242), GetTextValue(reg22));
    EXPECT_EQ(to_string(42000), GetTextValue(reg33));
}

TEST_F(TSerialClientTest, Write)
{
    PRegister reg1 = Reg(1);
    PRegister reg20 = Reg(20);
    SerialClient->AddDevice(Device);

    Note() << "Cycle()";
    SerialClient->Cycle();

    SerialClient->SetTextValue(reg1, "1");
    SerialClient->SetTextValue(reg20, "4242");

    for (int i = 0; i < 3; ++i) {
        Note() << "Cycle()";
        SerialClient->Cycle();

        EXPECT_EQ(to_string(1), GetTextValue(reg1));
        EXPECT_EQ(1, Device->Registers[1]);
        EXPECT_EQ(to_string(4242), GetTextValue(reg20));
        EXPECT_EQ(4242, Device->Registers[20]);
    }
}

TEST_F(TSerialClientTest, U8)
{
    PRegister reg20 = Reg(20, U8);
    PRegister reg30 = Reg(30, U8);
    SerialClient->AddDevice(Device);

    Note() << "server -> client: 10, 20";
    Device->Registers[20] = 10;
    Device->Registers[30] = 20;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), GetTextValue(reg20));
    EXPECT_EQ(to_string(20), GetTextValue(reg30));

    Note() << "server -> client: 0x2010, 0x2011";
    Device->Registers[20] = 0x2010;
    Device->Registers[30] = 0x2011;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x10), GetTextValue(reg20));
    EXPECT_EQ(to_string(0x11), GetTextValue(reg30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(reg20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), GetTextValue(reg20));
    EXPECT_EQ(10, Device->Registers[20]);

    Note() << "client -> server: 257";
    SerialClient->SetTextValue(reg20, "257");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(1), GetTextValue(reg20));
    EXPECT_EQ(1, Device->Registers[20]);
}

TEST_F(TSerialClientTest, S8)
{
    PRegister reg20 = Reg(20, S8);
    PRegister reg30 = Reg(30, S8);
    SerialClient->AddDevice(Device);

    Note() << "server -> client: 10, 20";
    Device->Registers[20] = 10;
    Device->Registers[30] = 20;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), GetTextValue(reg20));
    EXPECT_EQ(to_string(20), GetTextValue(reg30));

    Note() << "server -> client: -2, -3";
    Device->Registers[20] = 254;
    Device->Registers[30] = 253;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), GetTextValue(reg20));
    EXPECT_EQ(to_string(-3), GetTextValue(reg30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(reg20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), GetTextValue(reg20));
    EXPECT_EQ(10, Device->Registers[20]);

    Note() << "client -> server: -2";
    SerialClient->SetTextValue(reg20, "-2");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), GetTextValue(reg20));
    EXPECT_EQ(254, Device->Registers[20]);
}

TEST_F(TSerialClientTest, Char8)
{
    PRegister reg20 = Reg(20, Char8);
    PRegister reg30 = Reg(30, Char8);
    SerialClient->AddDevice(Device);

    Note() << "server -> client: 65, 66";
    Device->Registers[20] = 65;
    Device->Registers[30] = 66;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("A", GetTextValue(reg20));
    EXPECT_EQ("B", GetTextValue(reg30));

    Note() << "client -> server: '!'";
    SerialClient->SetTextValue(reg20, "!");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("!", GetTextValue(reg20));
    EXPECT_EQ(33, Device->Registers[20]);
}

TEST_F(TSerialClientTest, S64)
{
    PRegister reg20 = Reg(20, S64);
    PRegister reg30 = Reg(30, S64);
    SerialClient->AddDevice(Device);

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
    EXPECT_EQ(to_string(0x00AA00BB00CC00DD), GetTextValue(reg20));
    EXPECT_EQ(to_string(-1), GetTextValue(reg30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(reg20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), GetTextValue(reg20));
    EXPECT_EQ(0, Device->Registers[20]);
    EXPECT_EQ(0, Device->Registers[21]);
    EXPECT_EQ(0, Device->Registers[22]);
    EXPECT_EQ(10, Device->Registers[23]);

    Note() << "client -> server: -2";
    SerialClient->SetTextValue(reg20, "-2");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), GetTextValue(reg20));
    EXPECT_EQ(0xFFFF, Device->Registers[20]);
    EXPECT_EQ(0xFFFF, Device->Registers[21]);
    EXPECT_EQ(0xFFFF, Device->Registers[22]);
    EXPECT_EQ(0xFFFE, Device->Registers[23]);
}

TEST_F(TSerialClientTest, U64)
{
    PRegister reg20 = Reg(20, U64);
    PRegister reg30 = Reg(30, U64);
    SerialClient->AddDevice(Device);

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
    EXPECT_EQ(to_string(0x00AA00BB00CC00DD), GetTextValue(reg20));
    EXPECT_EQ("18446744073709551615", GetTextValue(reg30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(reg20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), GetTextValue(reg20));
    EXPECT_EQ(0, Device->Registers[20]);
    EXPECT_EQ(0, Device->Registers[21]);
    EXPECT_EQ(0, Device->Registers[22]);
    EXPECT_EQ(10, Device->Registers[23]);

    Note() << "client -> server: -2";
    SerialClient->SetTextValue(reg20, "-2");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("18446744073709551614", GetTextValue(reg20));
    EXPECT_EQ(0xFFFF, Device->Registers[20]);
    EXPECT_EQ(0xFFFF, Device->Registers[21]);
    EXPECT_EQ(0xFFFF, Device->Registers[22]);
    EXPECT_EQ(0xFFFE, Device->Registers[23]);
}

TEST_F(TSerialClientTest, S32)
{
    PRegister reg20 = Reg(20, S32);
    PRegister reg30 = Reg(30, S32);
    // create scaled register
    PRegister reg24 = Reg(24, S32, 0.001);

    SerialClient->AddDevice(Device);

    Note() << "server -> client: 10, 20";
    Device->Registers[20] = 0x00AA;
    Device->Registers[21] = 0x00BB;
    Device->Registers[30] = 0xFFFF;
    Device->Registers[31] = 0xFFFF;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB), GetTextValue(reg20));
    EXPECT_EQ(to_string(-1), GetTextValue(reg30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(reg20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), GetTextValue(reg20));
    EXPECT_EQ(0, Device->Registers[20]);
    EXPECT_EQ(10, Device->Registers[21]);

    Note() << "client -> server: -2";
    SerialClient->SetTextValue(reg20, "-2");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), GetTextValue(reg20));
    EXPECT_EQ(0xFFFF, Device->Registers[20]);
    EXPECT_EQ(0xFFFE, Device->Registers[21]);

    Note() << "client -> server: -0.123 (scaled)";
    SerialClient->SetTextValue(reg24, "-0.123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", GetTextValue(reg24));
    EXPECT_EQ(0xffff, Device->Registers[24]);
    EXPECT_EQ(0xff85, Device->Registers[25]);

    Note() << "server -> client: 0xffff 0xff85 (scaled)";
    Device->Registers[24] = 0xffff;
    Device->Registers[25] = 0xff85;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", GetTextValue(reg24));
}

TEST_F(TSerialClientTest, S24)
{
    PRegister reg20 = Reg(20, S24);
    PRegister reg30 = Reg(30, S24);
    // create scaled register
    PRegister reg24 = Reg(24, S24, 0.001);

    SerialClient->AddDevice(Device);

    Note() << "server -> client: 10, 20";
    Device->Registers[20] = 0x002A;
    Device->Registers[21] = 0x00BB;
    Device->Registers[30] = 0x00FF;
    Device->Registers[31] = 0xFFFF;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x002A00BB), GetTextValue(reg20));
    EXPECT_EQ(to_string(-1), GetTextValue(reg30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(reg20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), GetTextValue(reg20));
    EXPECT_EQ(0, Device->Registers[20]);
    EXPECT_EQ(10, Device->Registers[21]);

    Note() << "client -> server: -2";
    SerialClient->SetTextValue(reg20, "-2");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(-2), GetTextValue(reg20));
    EXPECT_EQ(0x00FF, Device->Registers[20]);
    EXPECT_EQ(0xFFFE, Device->Registers[21]);

    Note() << "client -> server: -0.123 (scaled)";
    SerialClient->SetTextValue(reg24, "-0.123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", GetTextValue(reg24));
    EXPECT_EQ(0x00FF, Device->Registers[24]);
    EXPECT_EQ(0xFF85, Device->Registers[25]);

    Note() << "server -> client: 0x00ff 0xff85 (scaled)";
    Device->Registers[24] = 0x00FF;
    Device->Registers[25] = 0xFF85;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", GetTextValue(reg24));
}

TEST_F(TSerialClientTest, U32)
{
    PRegister reg20 = Reg(20, U32);
    PRegister reg30 = Reg(30, U32);
    SerialClient->AddDevice(Device);

    Note() << "server -> client: 10, 20";
    Device->Registers[20] = 0x00AA;
    Device->Registers[21] = 0x00BB;
    Device->Registers[30] = 0xFFFF;
    Device->Registers[31] = 0xFFFF;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0x00AA00BB), GetTextValue(reg20));
    EXPECT_EQ(to_string(0xFFFFFFFF), GetTextValue(reg30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(reg20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(10), GetTextValue(reg20));
    EXPECT_EQ(0, Device->Registers[20]);
    EXPECT_EQ(10, Device->Registers[21]);

    Note() << "client -> server: -1 (overflow)";
    SerialClient->SetTextValue(reg20, "-1");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0xFFFFFFFF), GetTextValue(reg20));
    EXPECT_EQ(0xFFFF, Device->Registers[20]);
    EXPECT_EQ(0xFFFF, Device->Registers[21]);

    Device->Registers[22] = 123;
    Device->Registers[23] = 123;
    Note() << "client -> server: 4294967296 (overflow)";
    SerialClient->SetTextValue(reg20, "4294967296");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(0), GetTextValue(reg20));
    EXPECT_EQ(0, Device->Registers[20]);
    EXPECT_EQ(0, Device->Registers[21]);

    // boundaries check
    EXPECT_EQ(123, Device->Registers[22]);
    EXPECT_EQ(123, Device->Registers[23]);
}

TEST_F(TSerialClientTest, BCD32)
{
    PRegister reg20 = Reg(20, BCD32);
    SerialClient->AddDevice(Device);
    Device->Registers[22] = 123;
    Device->Registers[23] = 123;

    Note() << "server -> client: 0x1234 0x5678";
    Device->Registers[20] = 0x1234;
    Device->Registers[21] = 0x5678;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(12345678), GetTextValue(reg20));

    Note() << "client -> server: 12345678";
    SerialClient->SetTextValue(reg20, "12345678");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(12345678), GetTextValue(reg20));
    EXPECT_EQ(0x1234, Device->Registers[20]);
    EXPECT_EQ(0x5678, Device->Registers[21]);

    Note() << "client -> server: 567890";
    SerialClient->SetTextValue(reg20, "567890");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(567890), GetTextValue(reg20));
    EXPECT_EQ(0x0056, Device->Registers[20]);
    EXPECT_EQ(0x7890, Device->Registers[21]);

    // boundaries check
    EXPECT_EQ(123, Device->Registers[22]);
    EXPECT_EQ(123, Device->Registers[23]);
}

TEST_F(TSerialClientTest, BCD24)
{
    PRegister reg20 = Reg(20, BCD24);
    SerialClient->AddDevice(Device);
    Device->Registers[22] = 123;
    Device->Registers[23] = 123;

    Note() << "server -> client: 0x0034 0x5678";
    Device->Registers[20] = 0x0034;
    Device->Registers[21] = 0x5678;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(345678), GetTextValue(reg20));

    Note() << "client -> server: 567890";
    SerialClient->SetTextValue(reg20, "567890");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(567890), GetTextValue(reg20));
    EXPECT_EQ(0x0056, Device->Registers[20]);
    EXPECT_EQ(0x7890, Device->Registers[21]);

    // boundaries check
    EXPECT_EQ(123, Device->Registers[22]);
    EXPECT_EQ(123, Device->Registers[23]);
}

TEST_F(TSerialClientTest, BCD16)
{
    PRegister reg20 = Reg(20, BCD16);
    SerialClient->AddDevice(Device);
    Device->Registers[21] = 123;

    Note() << "server -> client: 0x1234";
    Device->Registers[20] = 0x1234;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(1234), GetTextValue(reg20));

    Note() << "client -> server: 1234";
    SerialClient->SetTextValue(reg20, "1234");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(1234), GetTextValue(reg20));
    EXPECT_EQ(0x1234, Device->Registers[20]);

    // boundaries check
    EXPECT_EQ(123, Device->Registers[21]);
}

TEST_F(TSerialClientTest, BCD8)
{
    PRegister reg20 = Reg(20, BCD8);
    SerialClient->AddDevice(Device);
    Device->Registers[21] = 123;

    Note() << "server -> client: 0x12";
    Device->Registers[20] = 0x12;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(12), GetTextValue(reg20));

    Note() << "client -> server: 12";
    SerialClient->SetTextValue(reg20, "12");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(to_string(12), GetTextValue(reg20));
    EXPECT_EQ(0x12, Device->Registers[20]);

    // boundaries check
    EXPECT_EQ(123, Device->Registers[21]);
}

TEST_F(TSerialClientTest, Float32)
{
    // create scaled register
    PRegister reg24 = Reg(24, Float, 100);
    PRegister reg20 = Reg(20, Float);
    PRegister reg30 = Reg(30, Float);
    SerialClient->AddDevice(Device);

    Note() << "server -> client: 0x45d2 0x0000, 0x449d 0x8000";
    Device->Registers[20] = 0x45d2;
    Device->Registers[21] = 0x0000;
    Device->Registers[30] = 0x449d;
    Device->Registers[31] = 0x8000;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("6720", GetTextValue(reg20));
    EXPECT_EQ("1260", GetTextValue(reg30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(reg20, "10");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("10", GetTextValue(reg20));
    EXPECT_EQ(0x4120, Device->Registers[20]);
    EXPECT_EQ(0x0000, Device->Registers[21]);

    Note() << "client -> server: -0.00123";
    SerialClient->SetTextValue(reg20, "-0.00123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.00123", GetTextValue(reg20));
    EXPECT_EQ(0xbaa1, Device->Registers[20]);
    EXPECT_EQ(0x37f4, Device->Registers[21]);

    Note() << "client -> server: -0.123 (scaled)";
    SerialClient->SetTextValue(reg24, "-0.123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", GetTextValue(reg24));
    EXPECT_EQ(0xbaa1, Device->Registers[24]);
    EXPECT_EQ(0x37f4, Device->Registers[25]);

    Note() << "server -> client: 0x449d 0x8000 (scaled)";
    Device->Registers[24] = 0x449d;
    Device->Registers[25] = 0x8000;
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("126000", GetTextValue(reg24));
}

TEST_F(TSerialClientTest, Double64)
{
    // create scaled register
    PRegister reg24 = Reg(24, Double, 100);
    PRegister reg20 = Reg(20, Double);
    PRegister reg30 = Reg(30, Double);
    SerialClient->AddDevice(Device);

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
    EXPECT_EQ("6720.123", GetTextValue(reg20));
    EXPECT_EQ("1260.321", GetTextValue(reg30));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(reg20, "10.9999");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("10.9999", GetTextValue(reg20));
    EXPECT_EQ(0x4025, Device->Registers[20]);
    EXPECT_EQ(0xfff2, Device->Registers[21]);
    EXPECT_EQ(0xe48e, Device->Registers[22]);
    EXPECT_EQ(0x8a72, Device->Registers[23]);

    Note() << "client -> server: -0.00123";
    SerialClient->SetTextValue(reg20, "-0.00123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.00123", GetTextValue(reg20));
    EXPECT_EQ(0xbf54, Device->Registers[20]);
    EXPECT_EQ(0x26fe, Device->Registers[21]);
    EXPECT_EQ(0x718a, Device->Registers[22]);
    EXPECT_EQ(0x86d7, Device->Registers[23]);

    Note() << "client -> server: -0.123 (scaled)";
    SerialClient->SetTextValue(reg24, "-0.123");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-0.123", GetTextValue(reg24));
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
    EXPECT_EQ("126000", GetTextValue(reg24));
}

TEST_F(TSerialClientTest, String)
{
    PRegister reg20 = Reg(20, String, 1, 0, 0, EWordOrder::BigEndian, EByteOrder::BigEndian, 0, 32 * 8);
    PRegister reg32 = Reg(62, String, 1, 0, 0, EWordOrder::BigEndian, EByteOrder::BigEndian, 0, 32 * 8);
    SerialClient->AddDevice(Device);

    Note() << "server -> client: 40ba401f7ced9168 , 4093b148b4395810";
    Device->Registers[20] = 'H';
    Device->Registers[21] = 'e';
    Device->Registers[22] = 'l';
    Device->Registers[23] = 'l';
    Device->Registers[24] = 'o';
    Device->Registers[25] = ',';
    Device->Registers[26] = ' ';
    Device->Registers[27] = 'w';
    Device->Registers[28] = 'o';
    Device->Registers[29] = 'r';
    Device->Registers[30] = 'l';
    Device->Registers[31] = 'd';
    Device->Registers[32] = '!';

    Device->Registers[62] = 'a';
    Device->Registers[63] = 'b';
    Device->Registers[64] = 'c';
    Device->Registers[65] = 'd';

    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ(std::string("Hello, world!"), GetTextValue(reg20));
    EXPECT_EQ("abcd", GetTextValue(reg32));

    Note() << "client -> server: 10";
    SerialClient->SetTextValue(reg20, "computer");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("computer", GetTextValue(reg20));
    EXPECT_EQ('c', static_cast<char>(Device->Registers[20]));
    EXPECT_EQ('o', static_cast<char>(Device->Registers[21]));
    EXPECT_EQ('m', static_cast<char>(Device->Registers[22]));
    EXPECT_EQ('p', static_cast<char>(Device->Registers[23]));
    EXPECT_EQ('u', static_cast<char>(Device->Registers[24]));
    EXPECT_EQ('t', static_cast<char>(Device->Registers[25]));
    EXPECT_EQ('e', static_cast<char>(Device->Registers[26]));
    EXPECT_EQ('r', static_cast<char>(Device->Registers[27]));
    EXPECT_EQ('\0', static_cast<char>(Device->Registers[28]));
}

TEST_F(TSerialClientTest, offset)
{
    // create scaled register with offset
    PRegister reg24 = Reg(24, S16, 3, -15);
    SerialClient->AddDevice(Device);

    Note() << "client -> server: -87 (scaled)";
    SerialClient->SetTextValue(reg24, "-87");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("-87", GetTextValue(reg24));
    EXPECT_EQ(0xffe8, Device->Registers[24]);
}

TEST_F(TSerialClientTest, Round)
{
    PRegister reg24_0_01 = Reg(24, Float, 1, 0, 0.01);
    PRegister reg26_1 = Reg(26, Float, 1, 0, 1);
    PRegister reg28_10 = Reg(28, Float, 1, 0, 10);
    PRegister reg30_0_2 = Reg(30, Float, 1, 0, 0.2);
    PRegister reg32_0_01 = Reg(32, Float, 1, 0, 0.01);

    SerialClient->AddDevice(Device);

    Note() << "client -> server: 12.345 (not rounded)";
    SerialClient->SetTextValue(reg24_0_01, "12.345");
    SerialClient->SetTextValue(reg26_1, "12.345");
    SerialClient->SetTextValue(reg28_10, "12.345");
    SerialClient->SetTextValue(reg30_0_2, "12.345");
    SerialClient->SetTextValue(reg32_0_01, "12.344");
    Note() << "Cycle()";
    SerialClient->Cycle();
    EXPECT_EQ("12.35", GetTextValue(reg24_0_01));
    EXPECT_EQ("12", GetTextValue(reg26_1));
    EXPECT_EQ("10", GetTextValue(reg28_10));
    EXPECT_EQ("12.4", GetTextValue(reg30_0_2));
    EXPECT_EQ("12.34", GetTextValue(reg32_0_01));

    union
    {
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
    SerialClient->AddDevice(Device);

    Note() << "Cycle() [first start]";
    SerialClient->Cycle();

    Device->BlockReadFor(20, true);
    Device->BlockWriteFor(20, true);

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
    reg20->GetConfig()->ErrorValue = TRegisterValue{42};
    Note() << "Cycle() [read, set error value for register]";
    SerialClient->Cycle();

    SerialClient->Cycle();
}

TEST_F(TSerialClientTestWithSetupRegisters, SetupOk)
{
    PRegister reg20 = Reg(20);
    SerialClient->AddDevice(Device);
    SerialClient->Cycle();
    EXPECT_EQ(Device->GetConnectionState(), TDeviceConnectionState::CONNECTED);
}

TEST_F(TSerialClientTestWithSetupRegisters, SetupFail)
{
    PRegister reg20 = Reg(20);
    SerialClient->AddDevice(Device);
    Device->BlockWriteFor(101, true);
    SerialClient->Cycle();
    EXPECT_EQ(Device->GetConnectionState(), TDeviceConnectionState::DISCONNECTED);
    Device->BlockWriteFor(101, false);
    SerialClient->Cycle();
    EXPECT_EQ(Device->GetConnectionState(), TDeviceConnectionState::CONNECTED);
}

class TSerialClientIntegrationTest: public TSerialClientTest
{
protected:
    void SetUp();
    void TearDown();
    void FilterConfig(const std::string& device_name);
    void Publish(const std::string& topic, const std::string& payload, uint8_t qos = 0, bool retain = true);
    void PublishWaitOnValue(const std::string& topic, const std::string& payload, uint8_t qos = 0, bool retain = true);

    void FixFakeDevices(PHandlerConfig config);

    /** reconnect test functions **/
    static void DeviceTimeoutOnly(const PSerialDevice& device, chrono::milliseconds timeout);
    static void DeviceMaxFailCyclesOnly(const PSerialDevice& device, int cycleCount);
    static void DeviceTimeoutAndMaxFailCycles(const PSerialDevice& device,
                                              chrono::milliseconds timeout,
                                              int cycleCount);

    PMQTTSerialDriver StartReconnectTest1Device(bool miss = false, bool pollIntervalTest = false);
    PMQTTSerialDriver StartReconnectTest2Devices();

    void ReconnectTest1Device(function<void()>&& thunk, bool pollIntervalTest = false);
    void ReconnectTest2Devices(function<void()>&& thunk);

    /**rpc request test functions**/
    TRPCResultCode SendRPCRequest(PMQTTSerialDriver serialDriver,
                                  std::vector<int> expectedRequest,
                                  std::vector<int> expectedResponse,
                                  size_t expectedResponseLength,
                                  std::chrono::seconds totalTimeout);

    PFakeMqttBroker MqttBroker;
    PFakeMqttClient MqttClient;
    PDeviceDriver Driver;
    PMQTTSerialDriver SerialDriver;
    PHandlerConfig Config;
    Json::Value CommonDeviceSchema;
    Json::Value PortsSchema;
    std::shared_ptr<TProtocolConfedSchemasMap> ProtocolSchemas;

    static const char* const Name;
    static const char* const OtherName;
};

const char* const TSerialClientIntegrationTest::Name = "serial-client-integration-test";
const char* const TSerialClientIntegrationTest::OtherName = "serial-client-integration-test-other";

void TSerialClientIntegrationTest::SetUp()
{
    SetMode(E_Unordered);
    TSerialClientTest::SetUp();
    std::filesystem::remove(DB_PATH);

    MqttBroker = NewFakeMqttBroker(*this);
    MqttClient = MqttBroker->MakeClient(Name);
    auto backend = NewDriverBackend(MqttClient);
    Driver = NewDriver(TDriverArgs{}
                           .SetId(Name)
                           .SetBackend(backend)
                           .SetIsTesting(true)
                           .SetReownUnknownDevices(true)
                           .SetUseStorage(true)
                           .SetStoragePath(DB_PATH));

    Driver->StartLoop();

    CommonDeviceSchema = WBMQTT::JSON::Parse(GetDataFilePath("../wb-mqtt-serial-confed-common.schema.json"));
    PortsSchema = WBMQTT::JSON::Parse(GetDataFilePath("../wb-mqtt-serial-ports.schema.json"));
    ProtocolSchemas = std::make_shared<TProtocolConfedSchemasMap>(GetDataFilePath("../protocols"), CommonDeviceSchema);
    ProtocolSchemas->AddFolder(GetDataFilePath("protocols"));
    AddRegisterType(CommonDeviceSchema, "fake");
    TTemplateMap t;

    Config = LoadConfig(GetDataFilePath("configs/config-test.json"),
                        DeviceFactory,
                        CommonDeviceSchema,
                        t,
                        rpcConfig,
                        PortsSchema,
                        *ProtocolSchemas,
                        [=](const Json::Value&, PRPCConfig rpcConfig) { return std::make_pair(Port, false); });

    FixFakeDevices(Config);
}

void TSerialClientIntegrationTest::TearDown()
{
    if (SerialDriver) {
        SerialDriver->ClearDevices();
    }
    Driver->StopLoop();
    TSerialClientTest::TearDown();
    std::filesystem::remove(DB_PATH);
}

void TSerialClientIntegrationTest::FixFakeDevices(PHandlerConfig config)
{
    for (auto port_config: config->PortConfigs) {
        PFakeSerialPort fakePort = std::dynamic_pointer_cast<TFakeSerialPort>(port_config->Port);
        if (fakePort) {
            for (auto device: port_config->Devices) {
                PFakeSerialDevice fakeDevice = std::dynamic_pointer_cast<TFakeSerialDevice>(device->Device);
                if (fakeDevice) {
                    fakeDevice->SetFakePort(fakePort);
                }
            }
        }
    }
}

void TSerialClientIntegrationTest::FilterConfig(const std::string& device_name)
{
    for (auto port_config: Config->PortConfigs) {
        LOG(Info) << "port devices: " << port_config->Devices.size();
        port_config->Devices.erase(
            remove_if(port_config->Devices.begin(),
                      port_config->Devices.end(),
                      [device_name](auto device) { return device->Device->DeviceConfig()->Name != device_name; }),
            port_config->Devices.end());
    }
    Config->PortConfigs.erase(remove_if(Config->PortConfigs.begin(),
                                        Config->PortConfigs.end(),
                                        [](PPortConfig port_config) { return port_config->Devices.empty(); }),
                              Config->PortConfigs.end());
    ASSERT_FALSE(Config->PortConfigs.empty()) << "device not found: " << device_name;
}

void TSerialClientIntegrationTest::DeviceTimeoutOnly(const PSerialDevice& device, chrono::milliseconds timeout)
{
    device->DeviceConfig()->DeviceTimeout = timeout;
    device->DeviceConfig()->DeviceMaxFailCycles = 0;
}

void TSerialClientIntegrationTest::DeviceMaxFailCyclesOnly(const PSerialDevice& device, int cycleCount)
{
    device->DeviceConfig()->DeviceTimeout = chrono::milliseconds(0);
    device->DeviceConfig()->DeviceMaxFailCycles = cycleCount;
}

void TSerialClientIntegrationTest::DeviceTimeoutAndMaxFailCycles(const PSerialDevice& device,
                                                                 chrono::milliseconds timeout,
                                                                 int cycleCount)
{
    device->DeviceConfig()->DeviceTimeout = timeout;
    device->DeviceConfig()->DeviceMaxFailCycles = cycleCount;
}

void TSerialClientIntegrationTest::Publish(const std::string& topic,
                                           const std::string& payload,
                                           uint8_t qos,
                                           bool retain)
{
    MqttBroker->Publish("em-test-other", {TMqttMessage{topic, payload, qos, retain}});
}

void TSerialClientIntegrationTest::PublishWaitOnValue(const std::string& topic,
                                                      const std::string& payload,
                                                      uint8_t qos,
                                                      bool retain)
{
    auto done = std::make_shared<WBMQTT::TPromise<void>>();
    Driver->On<WBMQTT::TControlOnValueEvent>([=](const WBMQTT::TControlOnValueEvent& event) {
        if (!done->IsFulfilled()) {
            done->Complete();
        }
    });
    Publish(topic, payload, qos, retain);
    done->GetFuture().Sync();
}

TEST_F(TSerialClientIntegrationTest, OnValue)
{
    FilterConfig("OnValueTest");

    SerialDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    auto device = TFakeSerialDevice::GetDevice("0x90");

    if (!device) {
        throw std::runtime_error("device not found or wrong type");
    }

    device->Registers[0] = 0;
    device->Registers[1] = 0;
    device->Registers[2] = 0;
    device->Registers[3] = 0;

    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();

    PublishWaitOnValue("/devices/OnValueTest/controls/Relay 1/on", "1", 0, true);
    PublishWaitOnValue("/devices/OnValueTest/controls/Relay 2/on", "1", 0, true);
    PublishWaitOnValue("/devices/OnValueTest/controls/Relay 3/on", "1", 0, true);

    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();

    ASSERT_EQ(500, device->Registers[0]);
    ASSERT_EQ(-1, device->Registers[1] << 16 | device->Registers[2]);
    ASSERT_EQ(-1, static_cast<int16_t>(device->Registers[3]));
}

TEST_F(TSerialClientIntegrationTest, OffValue)
{
    FilterConfig("OnValueTest");

    SerialDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    auto device = TFakeSerialDevice::GetDevice("0x90");

    if (!device) {
        throw std::runtime_error("device not found or wrong type");
    }

    device->Registers[0] = 500;
    device->Registers[1] = 0xFFFF;
    device->Registers[2] = 0xFFFF;
    device->Registers[3] = 0xFFFF;

    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();

    PublishWaitOnValue("/devices/OnValueTest/controls/Relay 1/on", "0", 0, true);
    PublishWaitOnValue("/devices/OnValueTest/controls/Relay 2/on", "0", 0, true);
    PublishWaitOnValue("/devices/OnValueTest/controls/Relay 3/on", "0", 0, true);

    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();

    ASSERT_EQ(200, device->Registers[0]);
    ASSERT_EQ(-2, device->Registers[1] << 16 | device->Registers[2]);
    ASSERT_EQ(-2, static_cast<int16_t>(device->Registers[3]));
}

TEST_F(TSerialClientIntegrationTest, OnValueError)
{
    FilterConfig("OnValueTest");

    SerialDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    auto device = TFakeSerialDevice::GetDevice("0x90");

    if (!device) {
        throw std::runtime_error("device not found or wrong type");
    }

    device->Registers[0] = 0;
    device->Registers[1] = 0;
    device->Registers[2] = 0;
    device->Registers[4] = 0;

    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();

    device->BlockWriteFor(0, true);

    PublishWaitOnValue("/devices/OnValueTest/controls/Relay 1/on", "1", 0, true);
    PublishWaitOnValue("/devices/OnValueTest/controls/Relay 2/on", "1", 0, true);
    PublishWaitOnValue("/devices/OnValueTest/controls/Relay 3/on", "1", 0, true);

    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();

    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();

    device->BlockWriteFor(0, false);

    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();

    ASSERT_EQ(500, device->Registers[0]);
    ASSERT_EQ(-1, device->Registers[1] << 16 | device->Registers[2]);
    ASSERT_EQ(-1, static_cast<int16_t>(device->Registers[3]));
}

TEST_F(TSerialClientIntegrationTest, Round)
{
    FilterConfig("RoundTest");

    SerialDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    auto device = TFakeSerialDevice::GetDevice("0x92");

    if (!device) {
        throw std::runtime_error("device not found or wrong type");
    }

    device->Registers[0] = 0;
    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();

    PublishWaitOnValue("/devices/RoundTest/controls/Float_0_01/on", "12.345", 0, true);
    PublishWaitOnValue("/devices/RoundTest/controls/Float_1/on", "12.345", 0, true);
    PublishWaitOnValue("/devices/RoundTest/controls/Float_10/on", "12.345", 0, true);
    PublishWaitOnValue("/devices/RoundTest/controls/Float_0_2/on", "12.345", 0, true);

    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();

    union
    {
        uint32_t words;
        float value;
    } data;

    data.words = device->Read2Registers(0);
    ASSERT_EQ(12.35f, data.value);

    data.words = device->Read2Registers(2);
    ASSERT_EQ(12.f, data.value);

    data.words = device->Read2Registers(4);
    ASSERT_EQ(10.f, data.value);

    data.words = device->Read2Registers(6);
    ASSERT_EQ(12.4f, data.value);
}

TEST_F(TSerialClientIntegrationTest, Errors)
{

    FilterConfig("DDL24");

    SerialDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    auto device = TFakeSerialDevice::GetDevice("23");

    if (!device) {
        throw std::runtime_error("device not found or wrong type");
    }

    Note() << "LoopOnce() [first start]";
    SerialDriver->LoopOnce();

    device->BlockReadFor(4, true);
    device->BlockWriteFor(4, true);
    device->BlockReadFor(7, true);
    device->BlockWriteFor(7, true);

    Note() << "LoopOnce() [read, rw blacklisted]";
    SerialDriver->LoopOnce();

    PublishWaitOnValue("/devices/ddl24/controls/RGB/on", "10;20;30", 0, true);
    PublishWaitOnValue("/devices/ddl24/controls/White/on", "42", 0, true);

    Note() << "LoopOnce() [write, rw blacklisted]";
    SerialDriver->LoopOnce();

    device->BlockReadFor(4, false);
    device->BlockWriteFor(4, false);
    device->BlockReadFor(7, false);
    device->BlockWriteFor(7, false);

    Note() << "LoopOnce() [read, nothing blacklisted]";
    SerialDriver->LoopOnce();

    PublishWaitOnValue("/devices/ddl24/controls/RGB/on", "10;20;30", 0, true);
    PublishWaitOnValue("/devices/ddl24/controls/White/on", "42", 0, true);

    Note() << "LoopOnce() [write, nothing blacklisted]";
    SerialDriver->LoopOnce();

    Note() << "LoopOnce() [read, nothing blacklisted] (2)";
    SerialDriver->LoopOnce();
}

TEST_F(TSerialClientIntegrationTest, PollIntervalMissErrors)
{
    FilterConfig("PollIntervalMissError");

    SerialDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    auto device = TFakeSerialDevice::GetDevice("0x97");

    if (!device) {
        throw std::runtime_error("device not found or wrong type");
    }

    Note() << "LoopOnce() [first start]";
    SerialDriver->LoopOnce();

    device->Registers[0] = 0xFF;

    // TODO: https://wirenboard.bitrix24.ru/company/personal/user/134/tasks/task/view/46912/
    for (size_t i = 0; i < 5; ++i) {
        this_thread::sleep_for(2s);

        Note() << "LoopOnce() [interval miss]";
        SerialDriver->LoopOnce();
    }

    for (size_t i = 0; i < 10; ++i) {
        this_thread::sleep_for(1s);

        Note() << "LoopOnce() [interval ok]";
        SerialDriver->LoopOnce();
    }
}

TEST_F(TSerialClientIntegrationTest, SetupErrors)
{
    FilterConfig("DDL24");

    SerialDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    auto device = TFakeSerialDevice::GetDevice("23");

    if (!device) {
        throw std::runtime_error("device not found or wrong type");
    }

    device->BlockWriteFor(1, true);

    Note() << "LoopOnce() [first start, write blacklisted]";
    SerialDriver->LoopOnce();

    device->BlockWriteFor(1, false);
    device->BlockWriteFor(2, true);

    Note() << "LoopOnce() [write blacklisted]";
    SerialDriver->LoopOnce();

    device->BlockWriteFor(2, false);

    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();
}

TEST_F(TSerialClientIntegrationTest, ErrorValue)
{
    FilterConfig("ErrorValueTest");

    SerialDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    auto device = TFakeSerialDevice::GetDevice("0x96");

    if (!device) {
        throw std::runtime_error("device not found or wrong type");
    }

    Note() << "LoopOnce()";
    SerialDriver->LoopOnce();

    device->Registers[0] = 0x7FFF;
    device->Registers[1] = 0x7FFF;
    device->Registers[2] = 0x7F;
    device->Registers[3] = 0x7F;
    device->Registers[4] = 0x7F;
    device->Registers[5] = 0xFFFF;
    device->Registers[6] = 0x7F;
    device->Registers[7] = 0xFFFF;
    device->Registers[8] = 0x7FFF;
    device->Registers[9] = 0xFFFF;
    device->Registers[10] = 0x7FFF;
    device->Registers[11] = 0xFFFF;
    device->Registers[12] = 0x7FFF;
    device->Registers[13] = 0xFFFF;
    device->Registers[14] = 0xFFFF;
    device->Registers[15] = 0xFFFF;
    device->Registers[16] = 0x7FFF;
    device->Registers[17] = 0xFFFF;
    device->Registers[18] = 0xFFFF;
    device->Registers[19] = 0xFFFF;
    device->Registers[20] = 0x7FFF;
    device->Registers[21] = 0xFFFF;
    device->Registers[22] = 0xFFFF;
    device->Registers[23] = 0xFFFF;
    device->Registers[24] = 0x7FFF;
    device->Registers[25] = 0xFFFF;

    Note() << "LoopOnce() [all errors]";
    SerialDriver->LoopOnce();
}

TEST_F(TSerialClientIntegrationTest, SlaveIdCollision)
{
    TTemplateMap t;

    auto factory = [=](const Json::Value& port_data, PRPCConfig rpcConfig) -> std::pair<PPort, bool> {
        auto path = port_data["path"].asString();
        return std::make_pair(std::make_shared<TFakeSerialPort>(*this, path, false), false);
    };

    EXPECT_THROW(LoadConfig(GetDataFilePath("configs/config-collision-test.json"),
                            DeviceFactory,
                            CommonDeviceSchema,
                            t,
                            rpcConfig,
                            PortsSchema,
                            *ProtocolSchemas,
                            factory),
                 TConfigParserException);

    EXPECT_THROW(LoadConfig(GetDataFilePath("configs/config-collision-test2.json"),
                            DeviceFactory,
                            CommonDeviceSchema,
                            t,
                            rpcConfig,
                            PortsSchema,
                            *ProtocolSchemas,
                            factory),
                 TConfigParserException);

    EXPECT_THROW(LoadConfig(GetDataFilePath("configs/config-collision-test3.json"),
                            DeviceFactory,
                            CommonDeviceSchema,
                            t,
                            rpcConfig,
                            PortsSchema,
                            *ProtocolSchemas,
                            factory),
                 TConfigParserException);

    EXPECT_NO_THROW(LoadConfig(GetDataFilePath("configs/config-no-collision-test.json"),
                               DeviceFactory,
                               CommonDeviceSchema,
                               t,
                               rpcConfig,
                               PortsSchema,
                               *ProtocolSchemas,
                               factory));
}

/* This function checks Serial Driver behaviour when RPC request and value publishing event occurs
 * Writing new values, then RPC IO through serial port and reading values are expected */

TRPCResultCode TSerialClientIntegrationTest::SendRPCRequest(PMQTTSerialDriver serialDriver,
                                                            std::vector<int> expectedRequest,
                                                            std::vector<int> expectedResponse,
                                                            size_t expectedResponseLength,
                                                            std::chrono::seconds totalTimeout)
{
    PublishWaitOnValue("/devices/RPCTest/controls/RGB/on", "10;20;30", 0, true);
    PublishWaitOnValue("/devices/RPCTest/controls/White/on", "42", 0, true);

    std::vector<PSerialPortDriver> portDrivers = serialDriver->GetPortDrivers();
    PSerialClient serialClient = portDrivers[0]->GetSerialClient();

    TRPCResultCode resultCode = TRPCResultCode::RPC_OK;
    std::vector<int> responseInt;
    Json::Value request;
    request["frame_timeout"] = 20;
    request["total_timeout"] = chrono::duration_cast<chrono::milliseconds>(totalTimeout).count();
    request["response_size"] = expectedResponseLength;
    std::vector<uint8_t> requestUint;
    std::copy(expectedRequest.begin(), expectedRequest.end(), back_inserter(requestUint));
    request["msg"] = FormatResponse(requestUint, TRPCMessageFormat::RPC_MESSAGE_FORMAT_HEX);
    request["format"] = "HEX";
    auto onResult = [&responseInt](const Json::Value& response) {
        auto str = HexStringToByteVector(response["response"].asString());
        std::copy(str.begin(), str.end(), back_inserter(responseInt));
    };
    auto onError = [&resultCode](const TMqttRpcErrorCode code, const std::string&) {
        resultCode =
            code == WBMQTT::E_RPC_REQUEST_TIMEOUT ? TRPCResultCode::RPC_WRONG_TIMEOUT : TRPCResultCode::RPC_WRONG_IO;
    };

    try {
        Note() << "Send RPC request";
        auto task = std::make_shared<TRPCPortLoadRawSerialClientTask>(request,
                                                                      onResult,
                                                                      onError,
                                                                      std::chrono::milliseconds(500));
        serialClient->AddTask(task);
        SerialDriver->LoopOnce();
        EXPECT_EQ(responseInt == expectedResponse, true);
    } catch (const TRPCException& exception) {
        resultCode = exception.GetResultCode();
    }

    return resultCode;
}

/* RPC Request sending test cases:
 * 1. ReadFrame timeout (port IO exception)
 * 2. RPC request timeout
 * 3. Successful RPC request execution
 * 4. Successful RPC request execution with zero length read
 */

TEST_F(TSerialClientIntegrationTest, RPCRequestTransceive)
{
    TTemplateMap t;

    Config = LoadConfig(GetDataFilePath("configs/config-rpc-test.json"),
                        DeviceFactory,
                        CommonDeviceSchema,
                        t,
                        rpcConfig,
                        PortsSchema,
                        *ProtocolSchemas,
                        [=](const Json::Value&, PRPCConfig rpcConfig) { return std::make_pair(Port, false); });
    FixFakeDevices(Config);

    FilterConfig("RPCTest");

    SerialDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    auto device = TFakeSerialDevice::GetDevice("0x98");

    if (!device) {
        throw std::runtime_error("device not found or wrong type");
    }

    device->SetSessionLogEnabled(true);
    std::vector<int> expectedRequest = {0x16, 0x05, 0x00, 0x0a, 0xff, 0x00, 0xaf, 0x1f};
    std::vector<int> expectedResponse = {0x16, 0x05, 0x00, 0x0a, 0xff, 0x00, 0xaf, 0x1f};
    std::vector<int> emptyVector;

    Note() << "LoopOnce() [first start]";
    SerialDriver->LoopOnce();
    SerialDriver->LoopOnce();

    // ReadFrame timeout case
    Note() << "[test case] ReadFrame exception: nothing to read";
    Port->Expect(expectedRequest, emptyVector, NULL);
    EXPECT_EQ(
        SendRPCRequest(SerialDriver, expectedRequest, emptyVector, expectedResponse.size(), std::chrono::seconds(12)),
        TRPCResultCode::RPC_WRONG_IO);

    // Successful case
    Note() << "[test case] RPC successful case";
    Port->Expect(expectedRequest, expectedResponse, NULL);
    EXPECT_EQ(SendRPCRequest(SerialDriver,
                             expectedRequest,
                             expectedResponse,
                             expectedResponse.size(),
                             std::chrono::seconds(12)),
              TRPCResultCode::RPC_OK);

    // Read zero length response
    Note() << "[test case] RPC request with zero length read";
    Port->Expect(expectedRequest, emptyVector, NULL);
    EXPECT_EQ(SendRPCRequest(SerialDriver, expectedRequest, emptyVector, 0, std::chrono::seconds(12)),
              TRPCResultCode::RPC_OK);
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

PMQTTSerialDriver TSerialClientIntegrationTest::StartReconnectTest1Device(bool miss, bool pollIntervalTest)
{
    TTemplateMap t;
    Config = LoadConfig(GetDataFilePath("configs/reconnect_test_1_device.json"),
                        DeviceFactory,
                        CommonDeviceSchema,
                        t,
                        rpcConfig,
                        PortsSchema,
                        *ProtocolSchemas,
                        [=](const Json::Value&, PRPCConfig config) { return std::make_pair(Port, false); });
    FixFakeDevices(Config);

    if (pollIntervalTest) {
        Config->PortConfigs[0]->Devices[0]->Channels[0]->Registers[0]->GetConfig()->ReadPeriod = 100s;
    }

    PMQTTSerialDriver mqttDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    auto device = TFakeSerialDevice::GetDevice("12");

    { // Test initial WriteInitValues
        Note() << "LoopOnce() [first start]";
        mqttDriver->LoopOnce();

        EXPECT_EQ(42, device->Registers[1]);
        EXPECT_EQ(24, device->Registers[2]);
    }

    { // Test read
        Note() << "LoopOnce()";
        mqttDriver->LoopOnce();
    }

    { // Device disconnected
        Note() << "SimulateDisconnect(true)";
        device->SetIsConnected(false);

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
                        mqttDriver->LoopOnce();
                        usleep(std::chrono::duration_cast<std::chrono::microseconds>(delay).count() /
                               device->DeviceConfig()->DeviceMaxFailCycles);
                    }
                } else {
                    auto disconnectTimepoint = std::chrono::steady_clock::now();

                    // Couple of unsuccessful reads
                    while (std::chrono::steady_clock::now() - disconnectTimepoint < delay) {
                        Note() << "LoopOnce()";
                        mqttDriver->LoopOnce();
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
                    mqttDriver->LoopOnce();
                }
            } else {
                auto disconnectTimepoint = std::chrono::steady_clock::now();
                auto delay = chrono::milliseconds(10000);

                // Couple of unsuccessful reads
                while (std::chrono::steady_clock::now() - disconnectTimepoint < delay) {
                    Note() << "LoopOnce()";
                    mqttDriver->LoopOnce();
                    usleep(std::chrono::duration_cast<std::chrono::microseconds>(delay).count() / 10);
                }
            }
        }

        // Final unsuccessful read after timeout, after this loop we expect device to be counted as disconnected
        Note() << "LoopOnce()";
        mqttDriver->LoopOnce();
    }

    return mqttDriver;
}

PMQTTSerialDriver TSerialClientIntegrationTest::StartReconnectTest2Devices()
{
    TTemplateMap t;
    Config = LoadConfig(GetDataFilePath("configs/reconnect_test_2_devices.json"),
                        DeviceFactory,
                        CommonDeviceSchema,
                        t,
                        rpcConfig,
                        PortsSchema,
                        *ProtocolSchemas,
                        [=](const Json::Value&, PRPCConfig config) { return std::make_pair(Port, false); });
    FixFakeDevices(Config);

    PMQTTSerialDriver mqttDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    auto dev1 = TFakeSerialDevice::GetDevice("12");
    auto dev2 = TFakeSerialDevice::GetDevice("13");

    { // Test initial setup
        Note() << "LoopOnce() [first start]";
        mqttDriver->LoopOnce();
        mqttDriver->LoopOnce();

        EXPECT_EQ(42, dev1->Registers[1]);
        EXPECT_EQ(24, dev1->Registers[2]);

        EXPECT_EQ(32, dev2->Registers[1]);
        EXPECT_EQ(64, dev2->Registers[2]);
    }

    { // Test read
        Note() << "LoopOnce()";
        mqttDriver->LoopOnce();
        mqttDriver->LoopOnce();
    }

    { // Device disconnected
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
                    mqttDriver->LoopOnce();
                    mqttDriver->LoopOnce();
                    usleep(std::chrono::duration_cast<std::chrono::microseconds>(Device->DeviceConfig()->DeviceTimeout)
                               .count() /
                           Device->DeviceConfig()->DeviceMaxFailCycles);
                }
            } else {
                auto disconnectTimepoint = std::chrono::steady_clock::now();

                // Couple of unsuccessful reads
                while (std::chrono::steady_clock::now() - disconnectTimepoint < Device->DeviceConfig()->DeviceTimeout) {
                    Note() << "LoopOnce()";
                    mqttDriver->LoopOnce();
                    mqttDriver->LoopOnce();
                    usleep(std::chrono::duration_cast<std::chrono::microseconds>(Device->DeviceConfig()->DeviceTimeout)
                               .count() /
                           10);
                }
            }
        } else if (by_cycles) {
            // Couple of unsuccessful reads
            auto remainingCycles = Device->DeviceConfig()->DeviceMaxFailCycles;

            while (remainingCycles--) {
                Note() << "LoopOnce()";
                mqttDriver->LoopOnce();
                mqttDriver->LoopOnce();
            }
        } else {
            auto disconnectTimepoint = std::chrono::steady_clock::now();
            auto delay = chrono::milliseconds(10000);

            // Couple of unsuccessful reads
            while (std::chrono::steady_clock::now() - disconnectTimepoint < delay) {
                Note() << "LoopOnce()";
                mqttDriver->LoopOnce();
                mqttDriver->LoopOnce();
                usleep(std::chrono::duration_cast<std::chrono::microseconds>(delay).count() / 10);
            }
        }

        // Final unsuccessful read after timeout, after this loop we expect device to be counted as disconnected
        Note() << "LoopOnce()";
        mqttDriver->LoopOnce();
        mqttDriver->LoopOnce();
    }

    return mqttDriver;
}

void TSerialClientIntegrationTest::ReconnectTest1Device(function<void()>&& thunk, bool pollIntervalTest)
{
    auto observer = StartReconnectTest1Device(false, pollIntervalTest);
    auto device = TFakeSerialDevice::GetDevice("12");

    device->Registers[1] = 0; // reset those to make sure that setup section is wrote again
    device->Registers[2] = 0;

    thunk();

    { // Loop to check limited polling
        Note() << "LoopOnce() (limited polling expected)";
        observer->LoopOnce();
    }

    { // Device is connected back
        Note() << "SimulateDisconnect(false)";
        device->SetIsConnected(true);

        Note() << "LoopOnce()";
        observer->LoopOnce();

        EXPECT_EQ(42, device->Registers[1]);
        EXPECT_EQ(24, device->Registers[2]);
    }
}

void TSerialClientIntegrationTest::ReconnectTest2Devices(function<void()>&& thunk)
{
    auto observer = StartReconnectTest2Devices();

    auto dev1 = TFakeSerialDevice::GetDevice("12");
    auto dev2 = TFakeSerialDevice::GetDevice("13");

    dev1->Registers[1] = 0; // reset those to make sure that setup section is wrote again
    dev1->Registers[2] = 0;

    dev2->Registers[1] = 1; // set control values to make sure that no setup section is written
    dev2->Registers[2] = 2;

    thunk();

    { // Loop to check limited polling
        Note() << "LoopOnce() (limited polling expected)";
        observer->LoopOnce();
        observer->LoopOnce();
    }
    { // Device is connected back
        Note() << "SimulateDisconnect(false)";
        dev1->SetIsConnected(true);

        Note() << "LoopOnce()";
        auto future = MqttBroker->WaitForPublish("/devices/reconnect-test-1/controls/I2/meta/error");
        observer->LoopOnce();
        observer->LoopOnce();

        EXPECT_EQ(42, dev1->Registers[1]);
        EXPECT_EQ(24, dev1->Registers[2]);

        EXPECT_EQ(1, dev2->Registers[1]);
        EXPECT_EQ(2, dev2->Registers[2]);
    }
}

TEST_F(TSerialClientIntegrationTest, ReconnectTimeout)
{
    ReconnectTest1Device([&] { DeviceTimeoutOnly(Device, DefaultDeviceTimeout); });
}

TEST_F(TSerialClientIntegrationTest, ReconnectCycles)
{
    ReconnectTest1Device([&] { DeviceMaxFailCyclesOnly(Device, 10); });
}

TEST_F(TSerialClientIntegrationTest, ReconnectTimeoutAndCycles)
{
    ReconnectTest1Device([&] { DeviceTimeoutAndMaxFailCycles(Device, DefaultDeviceTimeout, 10); });
}

TEST_F(TSerialClientIntegrationTest, ReconnectRegisterWithBigPollInterval)
{
    auto t1 = chrono::steady_clock::now();

    ReconnectTest1Device([&] { DeviceTimeoutOnly(Device, DefaultDeviceTimeout); }, true);

    auto time = chrono::steady_clock::now() - t1;

    EXPECT_LT(time, chrono::seconds(100));
}

TEST_F(TSerialClientIntegrationTest, Reconnect2)
{
    ReconnectTest2Devices([&] { DeviceTimeoutOnly(Device, DefaultDeviceTimeout); });
}

TEST_F(TSerialClientIntegrationTest, ReconnectMiss)
{
    auto observer = StartReconnectTest1Device(true);

    auto device = TFakeSerialDevice::GetDevice("12");
    if (!device) {
        throw std::runtime_error("device not found");
    }

    device->Registers[1] = 1; // set control values to make sure that no setup section is written
    device->Registers[2] = 2;

    { // Device is connected back before timeout
        Note() << "SimulateDisconnect(false)";
        device->SetIsConnected(true);

        Note() << "LoopOnce()";
        auto future = MqttBroker->WaitForPublish("/devices/reconnect-test/controls/I2");
        observer->LoopOnce();
        future.Wait();

        EXPECT_EQ(1, device->Registers[1]);
        EXPECT_EQ(2, device->Registers[2]);
    }
}

TEST_F(TSerialClientIntegrationTest, ReconnectOnPortWriteError)
{
    // The test simulates reconnection on EBADF error after writing first setup register
    // The behavior is a result of bad hwconf setup
    TTemplateMap t;
    Config = LoadConfig(GetDataFilePath("configs/reconnect_test_ebadf.json"),
                        DeviceFactory,
                        CommonDeviceSchema,
                        t,
                        rpcConfig,
                        PortsSchema,
                        *ProtocolSchemas,
                        [=](const Json::Value&, PRPCConfig config) { return std::make_pair(Port, false); });
    FixFakeDevices(Config);

    PMQTTSerialDriver mqttDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    Port->SimulateDisconnect(TFakeSerialPort::BadFileDescriptorOnWriteAndRead);

    for (auto i = 0; i < 3; ++i) {
        Note() << "LoopOnce()";
        mqttDriver->LoopOnce();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    for (auto i = 0; i < 3; ++i) {
        Note() << "LoopOnce()";
        mqttDriver->LoopOnce();
    }
}

TEST_F(TSerialClientIntegrationTest, OnTopicWriteError)
{
    // The test simulates EBADF error during register write after receiving a message from /on topic
    TTemplateMap t;
    Config = LoadConfig(GetDataFilePath("configs/reconnect_test_ebadf.json"),
                        DeviceFactory,
                        CommonDeviceSchema,
                        t,
                        rpcConfig,
                        PortsSchema,
                        *ProtocolSchemas,
                        [=](const Json::Value&, PRPCConfig config) { return std::make_pair(Port, false); });
    FixFakeDevices(Config);

    PMQTTSerialDriver mqttDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    Port->SimulateDisconnect(TFakeSerialPort::BadFileDescriptorOnWriteAndRead);

    Note() << "LoopOnce()";
    mqttDriver->LoopOnce();

    PublishWaitOnValue("/devices/test/controls/I1/on", "42", 1, true);

    Note() << "LoopOnce()";
    mqttDriver->LoopOnce();
}

TEST_F(TSerialClientIntegrationTest, ReconnectAfterNetworkDisconnect)
{
    // The test simulates reconnection after loosing network connection to TCP-based device
    // A socket is alive, writes are successful, reads fail with timeout
    // The logic must close an reopen port
    TTemplateMap t;
    Config = LoadConfig(GetDataFilePath("configs/reconnect_test_network.json"),
                        DeviceFactory,
                        CommonDeviceSchema,
                        t,
                        rpcConfig,
                        PortsSchema,
                        *ProtocolSchemas,
                        [=](const Json::Value&, PRPCConfig config) { return std::make_pair(Port, false); });
    FixFakeDevices(Config);

    PMQTTSerialDriver mqttDriver = make_shared<TMQTTSerialDriver>(Driver, Config);

    auto device = TFakeSerialDevice::GetDevice("12");
    if (!device) {
        throw std::runtime_error("device not found");
    }

    // successful reads
    for (auto i = 0; i < 3; ++i) {
        Note() << "LoopOnce()";
        mqttDriver->LoopOnce();
    }

    // read errors
    device->BlockReadFor(1, true);

    std::this_thread::sleep_for(std::chrono::milliseconds(70));

    for (auto i = 0; i < 3; ++i) {
        Note() << "LoopOnce()";
        mqttDriver->LoopOnce();
    }

    // device is disconnected
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    mqttDriver->LoopOnce();

    // port reopen
    device->BlockReadFor(1, false);

    for (auto i = 0; i < 3; ++i) {
        Note() << "LoopOnce()";
        mqttDriver->LoopOnce();
    }
}
