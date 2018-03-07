#include <string>
#include "fake_serial_port.h"
#include "mercury200_expectations.h"
#include "mercury200_device.h"
#include "protocol_register.h"


class TMercury200Test: public TSerialDeviceTest, public TMercury200Expectations
{
protected:
    void SetUp();
    void VerifyEnergyQuery();
    void VerifyParamQuery();

    virtual PDeviceConfig GetDeviceConfig();

    PMercury200Device Mercury200Dev;

    PProtocolRegister Mercury200RET1Reg;
    PProtocolRegister Mercury200RET2Reg;
    PProtocolRegister Mercury200RET3Reg;
    PProtocolRegister Mercury200RET4Reg;
    PProtocolRegister Mercury200UReg;
    PProtocolRegister Mercury200IReg;
    PProtocolRegister Mercury200PReg;
    PProtocolRegister Mercury200BatReg;
};

PDeviceConfig TMercury200Test::GetDeviceConfig()
{
    return std::make_shared<TDeviceConfig>("mercury200", std::to_string(22417438), "mercury200");
}

void TMercury200Test::SetUp()
{
    TSerialDeviceTest::SetUp();
    Mercury200Dev = std::make_shared<TMercury200Device>(GetDeviceConfig(), SerialPort,
                            TSerialDeviceFactory::GetProtocol("mercury200"));

    Mercury200RET1Reg = std::make_shared<TProtocolRegister>(0x2700, TMercury200Device::REG_PARAM_VALUE32);
    Mercury200RET2Reg = std::make_shared<TProtocolRegister>(0x2704, TMercury200Device::REG_PARAM_VALUE32);
    Mercury200RET3Reg = std::make_shared<TProtocolRegister>(0x2708, TMercury200Device::REG_PARAM_VALUE32);
    Mercury200RET4Reg = std::make_shared<TProtocolRegister>(0x270C, TMercury200Device::REG_PARAM_VALUE32);
    Mercury200UReg = std::make_shared<TProtocolRegister>(0x6300, TMercury200Device::REG_PARAM_VALUE16);
    Mercury200IReg = std::make_shared<TProtocolRegister>(0x6302, TMercury200Device::REG_PARAM_VALUE16);
    Mercury200PReg = std::make_shared<TProtocolRegister>(0x6304, TMercury200Device::REG_PARAM_VALUE24);
    Mercury200BatReg = std::make_shared<TProtocolRegister>(0x2900, TMercury200Device::REG_PARAM_VALUE16);

    SerialPort->Open();
}

void TMercury200Test::VerifyEnergyQuery()
{
    EnqueueMercury200EnergyResponse();
    ASSERT_EQ(0x62142, Mercury200Dev->ReadProtocolRegister(Mercury200RET1Reg));
    ASSERT_EQ(0x20834, Mercury200Dev->ReadProtocolRegister(Mercury200RET2Reg));
    ASSERT_EQ(0x11111, Mercury200Dev->ReadProtocolRegister(Mercury200RET3Reg));
    ASSERT_EQ(0x22222, Mercury200Dev->ReadProtocolRegister(Mercury200RET4Reg));
    Mercury200Dev->EndPollCycle();
}

void TMercury200Test::VerifyParamQuery()
{
    EnqueueMercury200ParamResponse();
    ASSERT_EQ(0x1234, Mercury200Dev->ReadProtocolRegister(Mercury200UReg));
    ASSERT_EQ(0x5678, Mercury200Dev->ReadProtocolRegister(Mercury200IReg));
    ASSERT_EQ(0x765432, Mercury200Dev->ReadProtocolRegister(Mercury200PReg));
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
    ASSERT_EQ(0x0391, Mercury200Dev->ReadProtocolRegister(Mercury200BatReg));
    Mercury200Dev->EndPollCycle();
    SerialPort->Close();
}


class TMercury200IntegrationTest: public TSerialDeviceIntegrationTest, public TMercury200Expectations
{
protected:
    void SetUp();
    void TearDown();
    const char* ConfigPath() const { return "configs/config-mercury200-test.json"; }
    const char* GetTemplatePath() const { return "../wb-mqtt-serial-templates/"; }
};

void TMercury200IntegrationTest::SetUp()
{
    TSerialDeviceIntegrationTest::SetUp();
    Observer->SetUp();
    SerialPort->SetExpectedFrameTimeout(std::chrono::milliseconds(150));
    ASSERT_TRUE(!!SerialPort);
}

void TMercury200IntegrationTest::TearDown()
{
    SerialPort->Close();
    TSerialDeviceIntegrationTest::TearDown();
}


TEST_F(TMercury200IntegrationTest, Poll)
{
    EnqueueMercury200BatteryVoltageResponse();
    EnqueueMercury200ParamResponse();
    EnqueueMercury200EnergyResponse();
    Note() << "LoopOnce()";
    Observer->LoopOnce();
}
