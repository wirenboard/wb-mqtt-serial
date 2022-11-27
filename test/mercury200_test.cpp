#include "devices/mercury200_device.h"
#include "fake_serial_port.h"
#include "mercury200_expectations.h"
#include <string>

class TMercury200Test: public TSerialDeviceTest, public TMercury200Expectations
{
protected:
    void SetUp();
    void VerifyEnergyQuery();
    void VerifyParamQuery();

    virtual PDeviceConfig GetDeviceConfig() const;

    PMercury200Device Mercury200Dev;

    PRegister Mercury200RET1Reg;
    PRegister Mercury200RET2Reg;
    PRegister Mercury200RET3Reg;
    PRegister Mercury200RET4Reg;
    PRegister Mercury200UReg;
    PRegister Mercury200IReg;
    PRegister Mercury200PReg;
    PRegister Mercury200BatReg;
};

PDeviceConfig TMercury200Test::GetDeviceConfig() const
{
    return std::make_shared<TDeviceConfig>("mercury200", "0xFE123456", "mercury200");
}

void TMercury200Test::SetUp()
{
    TSerialDeviceTest::SetUp();
    Mercury200Dev =
        std::make_shared<TMercury200Device>(GetDeviceConfig(), SerialPort, DeviceFactory.GetProtocol("mercury200"));

    Mercury200RET1Reg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create(0, 0x27, BCD32));
    Mercury200RET2Reg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create(0, 0x27, BCD32));
    Mercury200RET2Reg->SetDataOffset(4);
    Mercury200RET3Reg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create(0, 0x27, BCD32));
    Mercury200RET3Reg->SetDataOffset(8);
    Mercury200RET4Reg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create(0, 0x27, BCD32));
    Mercury200RET4Reg->SetDataOffset(12);
    Mercury200UReg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create(0, 0x63, BCD16));
    Mercury200IReg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create(0, 0x63, BCD16));
    Mercury200IReg->SetDataOffset(2);
    Mercury200PReg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create(0, 0x63, BCD24));
    Mercury200PReg->SetDataOffset(4);
    Mercury200BatReg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create(0, 0x29, BCD16));

    SerialPort->Open();
}

void TMercury200Test::VerifyEnergyQuery()
{
    EnqueueMercury200EnergyResponse();
    ASSERT_EQ(TRegisterValue{0x62142}, Mercury200Dev->ReadRegisterImpl(Mercury200RET1Reg));
    ASSERT_EQ(TRegisterValue{0x20834}, Mercury200Dev->ReadRegisterImpl(Mercury200RET2Reg));
    ASSERT_EQ(TRegisterValue{0x11111}, Mercury200Dev->ReadRegisterImpl(Mercury200RET3Reg));
    ASSERT_EQ(TRegisterValue{0x22222}, Mercury200Dev->ReadRegisterImpl(Mercury200RET4Reg));
    Mercury200Dev->InvalidateReadCache();
}

void TMercury200Test::VerifyParamQuery()
{
    EnqueueMercury200ParamResponse();
    ASSERT_EQ(TRegisterValue{0x1234}, Mercury200Dev->ReadRegisterImpl(Mercury200UReg));
    ASSERT_EQ(TRegisterValue{0x5678}, Mercury200Dev->ReadRegisterImpl(Mercury200IReg));
    ASSERT_EQ(TRegisterValue{0x765432}, Mercury200Dev->ReadRegisterImpl(Mercury200PReg));
    Mercury200Dev->InvalidateReadCache();
}

TEST_F(TMercury200Test, EnergyQuery)
{
    try {
        VerifyEnergyQuery();
    } catch (const std::exception& e) {
        SerialPort->Close();
        throw e;
    }
    SerialPort->Close();
}

TEST_F(TMercury200Test, ParamsQuery)
{
    try {
        VerifyParamQuery();
    } catch (const std::exception& e) {
        SerialPort->Close();
        throw e;
    }
    SerialPort->Close();
}

TEST_F(TMercury200Test, BatteryVoltageQuery)
{
    EnqueueMercury200BatteryVoltageResponse();
    ASSERT_EQ(TRegisterValue{0x0391}, Mercury200Dev->ReadRegisterImpl(Mercury200BatReg));
    SerialPort->Close();
}

class TMercury200IntegrationTest: public TSerialDeviceIntegrationTest, public TMercury200Expectations
{
protected:
    void SetUp();
    void TearDown();
    const char* ConfigPath() const
    {
        return "configs/config-mercury200-test.json";
    }
    std::string GetTemplatePath() const override
    {
        return "../templates";
    }
};

void TMercury200IntegrationTest::SetUp()
{
    TSerialDeviceIntegrationTest::SetUp();
    SerialPort->SetExpectedFrameTimeout(std::chrono::milliseconds(7));
    ASSERT_TRUE(!!SerialPort);
}

void TMercury200IntegrationTest::TearDown()
{
    SerialPort->Close();
    TSerialDeviceIntegrationTest::TearDown();
}

TEST_F(TMercury200IntegrationTest, Poll)
{
    EnqueueMercury200EnergyResponse();
    EnqueueMercury200BatteryVoltageResponse();
    EnqueueMercury200ParamResponse();
    Note() << "LoopOnce()";
    for (auto i = 0; i < 3; ++i) {
        SerialDriver->LoopOnce();
    }
}