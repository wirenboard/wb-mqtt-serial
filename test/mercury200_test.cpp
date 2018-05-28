#include <string>
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

    PVirtualRegister Mercury200RET1Reg;
    PVirtualRegister Mercury200RET2Reg;
    PVirtualRegister Mercury200RET3Reg;
    PVirtualRegister Mercury200RET4Reg;
    PVirtualRegister Mercury200UReg;
    PVirtualRegister Mercury200IReg;
    PVirtualRegister Mercury200PReg;
    PVirtualRegister Mercury200BatReg;
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

    Mercury200RET4Reg = Reg(Mercury200Dev, 0x27, TMercury200Device::MEM_ENERGY, U32, 1, 0, 0, EWordOrder::BigEndian, 0, 32);
    Mercury200RET3Reg = Reg(Mercury200Dev, 0x27, TMercury200Device::MEM_ENERGY, U32, 1, 0, 0, EWordOrder::BigEndian, 32, 32);
    Mercury200RET2Reg = Reg(Mercury200Dev, 0x27, TMercury200Device::MEM_ENERGY, U32, 1, 0, 0, EWordOrder::BigEndian, 64, 32);
    Mercury200RET1Reg = Reg(Mercury200Dev, 0x27, TMercury200Device::MEM_ENERGY, U32, 1, 0, 0, EWordOrder::BigEndian, 96, 32);
    Mercury200PReg = Reg(Mercury200Dev, 0x63, TMercury200Device::MEM_PARAMS, U24, 1, 0, 0, EWordOrder::BigEndian, 0, 24);
    Mercury200IReg = Reg(Mercury200Dev, 0x63, TMercury200Device::MEM_PARAMS, U16, 1, 0, 0, EWordOrder::BigEndian, 24, 16);
    Mercury200UReg = Reg(Mercury200Dev, 0x63, TMercury200Device::MEM_PARAMS, U16, 1, 0, 0, EWordOrder::BigEndian, 40, 16);
    Mercury200BatReg = Reg(Mercury200Dev, 0x29, TMercury200Device::REG_PARAM_16);

    SerialPort->Open();
}

void TMercury200Test::VerifyEnergyQuery()
{
    auto Mercury200EnergyMBQuery = GetReadQuery({
        Mercury200RET1Reg, Mercury200RET2Reg,
        Mercury200RET3Reg, Mercury200RET4Reg
    });

    ASSERT_EQ(1, Mercury200EnergyMBQuery->GetBlockCount());

    EnqueueMercury200EnergyResponse();
    TestRead(Mercury200EnergyMBQuery);

    ASSERT_EQ(0x62142, Mercury200RET1Reg->GetValue());
    ASSERT_EQ(0x20834, Mercury200RET2Reg->GetValue());
    ASSERT_EQ(0x11111, Mercury200RET3Reg->GetValue());
    ASSERT_EQ(0x22222, Mercury200RET4Reg->GetValue());
    Mercury200Dev->EndPollCycle();
}

void TMercury200Test::VerifyParamQuery()
{
    auto Mercury200ParamsQuery = GetReadQuery({
        Mercury200UReg, Mercury200IReg,
        Mercury200PReg
    });

    ASSERT_EQ(1, Mercury200ParamsQuery->GetBlockCount());

    EnqueueMercury200ParamResponse();
    TestRead(Mercury200ParamsQuery);

    ASSERT_EQ(0x1234, Mercury200UReg->GetValue());
    ASSERT_EQ(0x5678, Mercury200IReg->GetValue());
    ASSERT_EQ(0x765432, Mercury200PReg->GetValue());
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
    auto Mercury200BatRegQuery = GetReadQuery({ Mercury200BatReg });

    EnqueueMercury200BatteryVoltageResponse();
    TestRead(Mercury200BatRegQuery);
    ASSERT_EQ(0x0391, Mercury200BatReg->GetValue());
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
    EnqueueMercury200EnergyResponse();
    EnqueueMercury200ParamResponse();
    EnqueueMercury200BatteryVoltageResponse();
    Note() << "LoopOnce()";
    Observer->LoopOnce();
}
