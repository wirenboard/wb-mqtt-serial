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

    Mercury200RET1Reg = Mercury200Dev->AddRegister(TRegisterConfig::Create(0, 0x27, BCD32));
    TRegisterDesc regAddress;
    regAddress.Address = std::make_shared<TUint32RegisterAddress>(0x27);
    regAddress.DataOffset = 4;
    Mercury200RET2Reg = Mercury200Dev->AddRegister(TRegisterConfig::Create(0, regAddress, BCD32));
    regAddress.DataOffset = 8;
    Mercury200RET3Reg = Mercury200Dev->AddRegister(TRegisterConfig::Create(0, regAddress, BCD32));
    regAddress.DataOffset = 12;
    Mercury200RET4Reg = Mercury200Dev->AddRegister(TRegisterConfig::Create(0, regAddress, BCD32));
    Mercury200UReg = Mercury200Dev->AddRegister(TRegisterConfig::Create(0, 0x63, BCD16));
    regAddress.Address = std::make_shared<TUint32RegisterAddress>(0x63);
    regAddress.DataOffset = 2;
    Mercury200IReg = Mercury200Dev->AddRegister(TRegisterConfig::Create(0, regAddress, BCD16));
    regAddress.DataOffset = 4;
    Mercury200PReg = Mercury200Dev->AddRegister(TRegisterConfig::Create(0, regAddress, BCD24));
    Mercury200BatReg = Mercury200Dev->AddRegister(TRegisterConfig::Create(0, 0x29, BCD16));

    SerialPort->Open();
}

void TMercury200Test::VerifyEnergyQuery()
{
    EnqueueMercury200EnergyResponse();
    ASSERT_EQ(TRegisterValue{0x62142}, Mercury200Dev->ReadRegisterImpl(*Mercury200RET1Reg->GetConfig()));
    ASSERT_EQ(TRegisterValue{0x20834}, Mercury200Dev->ReadRegisterImpl(*Mercury200RET2Reg->GetConfig()));
    ASSERT_EQ(TRegisterValue{0x11111}, Mercury200Dev->ReadRegisterImpl(*Mercury200RET3Reg->GetConfig()));
    ASSERT_EQ(TRegisterValue{0x22222}, Mercury200Dev->ReadRegisterImpl(*Mercury200RET4Reg->GetConfig()));
    Mercury200Dev->InvalidateReadCache();
}

void TMercury200Test::VerifyParamQuery()
{
    EnqueueMercury200ParamResponse();
    ASSERT_EQ(TRegisterValue{0x1234}, Mercury200Dev->ReadRegisterImpl(*Mercury200UReg->GetConfig()));
    ASSERT_EQ(TRegisterValue{0x5678}, Mercury200Dev->ReadRegisterImpl(*Mercury200IReg->GetConfig()));
    ASSERT_EQ(TRegisterValue{0x765432}, Mercury200Dev->ReadRegisterImpl(*Mercury200PReg->GetConfig()));
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
    ASSERT_EQ(TRegisterValue{0x0391}, Mercury200Dev->ReadRegisterImpl(*Mercury200BatReg->GetConfig()));
    SerialPort->Close();
}

class TMercury200IntegrationTest: public TSerialDeviceIntegrationTest, public TMercury200Expectations
{
protected:
    void SetUp() override;
    void TearDown() override;
    const char* ConfigPath() const override
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