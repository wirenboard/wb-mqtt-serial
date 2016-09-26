#include "fake_serial_port.h"
#include "mercury200_expectations.h"
#include "mercury200_device.h"

class TMercury200Test: public TSerialDeviceTest, public TMercury200Expectations
{
protected:
    void SetUp();
    void VerifyEnergyQuery();
    void VerifyParamQuery();

    virtual PDeviceConfig GetDeviceConfig();

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

PDeviceConfig TMercury200Test::GetDeviceConfig()
{
    return std::make_shared<TDeviceConfig>("mercury200", 123456, "mercury200");
}

void TMercury200Test::SetUp()
{
    TSerialDeviceTest::SetUp();
    Mercury200Dev = std::make_shared<TMercury200Device>(GetDeviceConfig(), SerialPort, 
                            TSerialDeviceFactory::GetProtocol("mercury200"));

    Mercury200RET1Reg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create( TMercury200Device::REG_PARAM_VALUE32, 0x2700, BCD32));
    Mercury200RET2Reg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create( TMercury200Device::REG_PARAM_VALUE32, 0x2704, BCD32));
    Mercury200RET3Reg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create( TMercury200Device::REG_PARAM_VALUE32, 0x2708, BCD32));
    Mercury200RET4Reg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create( TMercury200Device::REG_PARAM_VALUE32, 0x270C, BCD32));
    Mercury200UReg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create( TMercury200Device::REG_PARAM_VALUE16, 0x6300, BCD16));
    Mercury200IReg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create( TMercury200Device::REG_PARAM_VALUE16, 0x6302, BCD16));
    Mercury200PReg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create( TMercury200Device::REG_PARAM_VALUE24, 0x6304, BCD24));
    Mercury200BatReg = TRegister::Intern(Mercury200Dev, TRegisterConfig::Create( TMercury200Device::REG_PARAM_VALUE16, 0x2900, BCD16));

    SerialPort->Open();
}

void TMercury200Test::VerifyEnergyQuery()
{
    EnqueueMercury200EnergyResponse();
    ASSERT_EQ(0x62142, Mercury200Dev->ReadRegister(Mercury200RET1Reg));
    ASSERT_EQ(0x20834, Mercury200Dev->ReadRegister(Mercury200RET2Reg));
    ASSERT_EQ(0x11111, Mercury200Dev->ReadRegister(Mercury200RET3Reg));
    ASSERT_EQ(0x22222, Mercury200Dev->ReadRegister(Mercury200RET4Reg));
    Mercury200Dev->EndPollCycle();
}

void TMercury200Test::VerifyParamQuery()
{
    EnqueueMercury200ParamResponse();
    ASSERT_EQ(0x1234, Mercury200Dev->ReadRegister(Mercury200UReg));
    ASSERT_EQ(0x5678, Mercury200Dev->ReadRegister(Mercury200IReg));
    ASSERT_EQ(0x765432, Mercury200Dev->ReadRegister(Mercury200PReg));
    Mercury200Dev->EndPollCycle();
}


TEST_F(TMercury200Test, EnergyQuery)
{
    try {
        VerifyEnergyQuery();
    } catch(const std::exception& e) {
        SerialPort->Close();
        throw e;
    }
    SerialPort->Close();
}

TEST_F(TMercury200Test, ParamsQuery)
{
    try {
        VerifyParamQuery();
    } catch(const std::exception& e) {
        SerialPort->Close();
        throw e;
    }
    SerialPort->Close();
}

TEST_F(TMercury200Test, BatteryVoltageQuery)
{
    EnqueueMercury200BatteryVoltageResponse();
    ASSERT_EQ(0x0391, Mercury200Dev->ReadRegister(Mercury200BatReg));
    Mercury200Dev->EndPollCycle();
    SerialPort->Close();
}