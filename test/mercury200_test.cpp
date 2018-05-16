#include <string>
#include "fake_serial_port.h"
#include "mercury200_expectations.h"
#include "mercury200_device.h"
#include "memory_block.h"


class TMercury200Test: public TSerialDeviceTest, public TMercury200Expectations
{
protected:
    void SetUp();
    void VerifyEnergyQuery();
    void VerifyParamQuery();

    virtual PDeviceConfig GetDeviceConfig();

    PMercury200Device Mercury200Dev;

    PMemoryBlock Mercury200EnergyMB;
    PMemoryBlock Mercury200ParamsMB;
    PMemoryBlock Mercury200BatReg;
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

    Mercury200EnergyMB = Mercury200Dev->GetCreateMemoryBlock(0x27, TMercury200Device::MEM_ENERGY);
    Mercury200ParamsMB = Mercury200Dev->GetCreateMemoryBlock(0x6300, TMercury200Device::MEM_PARAMS);
    Mercury200BatReg = Mercury200Dev->GetCreateMemoryBlock(0x2900, TMercury200Device::REG_PARAM_16);

    SerialPort->Open();
}

void TMercury200Test::VerifyEnergyQuery()
{
    auto Mercury200EnergyMBQuery = GetReadQuery({ Mercury200EnergyMB });

    EnqueueMercury200EnergyResponse();
    const auto values = TestRead(Mercury200EnergyMBQuery);
    ASSERT_EQ(4, values.size());

    ASSERT_EQ(0x62142, values[0]);
    ASSERT_EQ(0x20834, values[1]);
    ASSERT_EQ(0x11111, values[2]);
    ASSERT_EQ(0x22222, values[3]);
    Mercury200Dev->EndPollCycle();
}

void TMercury200Test::VerifyParamQuery()
{
    auto Mercury200ParamsQuery = GetReadQuery({ Mercury200ParamsMB });

    EnqueueMercury200ParamResponse();
    const auto values = TestRead(Mercury200ParamsQuery);
    ASSERT_EQ(3, values.size());

    ASSERT_EQ(0x1234, values[0]);
    ASSERT_EQ(0x5678, values[1]);
    ASSERT_EQ(0x765432, values[2]);
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
    ASSERT_EQ(0x0391, TestRead(Mercury200BatRegQuery)[0]);
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
