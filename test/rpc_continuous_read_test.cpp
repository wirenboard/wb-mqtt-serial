#include "devices/modbus_device.h"
#include "fake_serial_port.h"
#include "modbus_expectations_base.h"
#include "rpc/rpc_device_handler.h"
#include "rpc/rpc_helpers.h"
#include "templates_map.h"
#include "test_utils.h"

#include <gtest/gtest.h>

namespace
{
    const uint8_t SLAVE_ID = 1;
    const uint16_t CONTINUOUS_READ_ADDRESS = 114;
    const uint16_t PARAMETER_ADDRESS = 200;
    const int ILLEGAL_DATA_ADDRESS_CODE = 0x02;
}

class TRPCContinuousReadTest: public TSerialDeviceTest, public TModbusExpectationsBase
{
protected:
    void SetUp() override
    {
        SelectModbusType(MODBUS_RTU);
        SetModbusRTUSlaveId(SLAVE_ID);
        TSerialDeviceTest::SetUp();
        SerialPort->Open();
        TemplateMap = std::make_unique<TTemplateMap>(GetTemplatesSchema());
        TemplateMap->AddTemplatesDir(TLoggedFixture::GetDataFilePath("device_load_config_test/templates"), false);
        DeviceTemplate = TemplateMap->GetTemplate("parameters_array");
    }

    void CreateDevice(bool enableWbContinuousRead)
    {
        TModbusDeviceConfig config;
        config.CommonConfig = std::make_shared<TDeviceConfig>("modbus", std::to_string(SLAVE_ID), "modbus");
        config.EnableWbContinuousRead = enableWbContinuousRead;
        Device = std::make_shared<TModbusDevice>(std::make_unique<Modbus::TModbusRTUTraits>(),
                                                 config,
                                                 DeviceFactory.GetProtocol("modbus"));
        Parameter = Device->AddRegister(TRegisterConfig::Create(Modbus::REG_HOLDING, PARAMETER_ADDRESS, U16));
    }

    void EnqueueHoldingRead(uint16_t address, uint16_t value)
    {
        Expector()->Expect(
            WrapPDU({0x03, static_cast<int>(address >> 8), static_cast<int>(address & 0xFF), 0x00, 0x01}),
            WrapPDU({0x03, 0x02, static_cast<int>(value >> 8), static_cast<int>(value & 0xFF)}),
            __func__);
    }

    void EnqueueHoldingReadException(uint16_t address)
    {
        Expector()->Expect(
            WrapPDU({0x03, static_cast<int>(address >> 8), static_cast<int>(address & 0xFF), 0x00, 0x01}),
            WrapPDU({0x83, ILLEGAL_DATA_ADDRESS_CODE}),
            __func__);
    }

    void EnqueueHoldingWrite(uint16_t address, uint16_t value)
    {
        std::vector<int> pdu = {0x06,
                                static_cast<int>(address >> 8),
                                static_cast<int>(address & 0xFF),
                                static_cast<int>(value >> 8),
                                static_cast<int>(value & 0xFF)};
        Expector()->Expect(WrapPDU(pdu), WrapPDU(pdu), __func__);
    }

    TRPCRegisterList MakeRegisterList(uint16_t value)
    {
        Parameter->SetValue(TRegisterValue(value));
        return {TRPCRegister{"parameter", std::string(), Parameter, true}};
    }

    std::unique_ptr<TTemplateMap> TemplateMap;
    PDeviceTemplate DeviceTemplate;
    std::shared_ptr<TModbusDevice> Device;
    PRegister Parameter;
};

/**
 * Checks that continuous read mode is restored with the value read from the device,
 * not with a hardcoded one.
 */
TEST_F(TRPCContinuousReadTest, RestoresModeValue)
{
    CreateDevice(true);
    EnqueueHoldingRead(CONTINUOUS_READ_ADDRESS, 2);
    Device->Prepare(*SerialPort, TDevicePrepareMode::WITHOUT_SETUP);
    ASSERT_EQ(Device->GetContinuousReadStatus(), TContinuousReadStatus::ENABLED);

    EnqueueHoldingWrite(CONTINUOUS_READ_ADDRESS, 0);
    EnqueueHoldingReadException(PARAMETER_ADDRESS);
    EnqueueHoldingWrite(CONTINUOUS_READ_ADDRESS, 2);

    TRPCDeviceRequest request(DeviceFactory.GetProtocolParams("modbus"), Device, DeviceTemplate, false);
    auto registerList = MakeRegisterList(0xFFFE);
    Json::Value data;
    MarkUnsupportedRegisterItems(*SerialPort, request, registerList, data);

    ASSERT_EQ(data["parameter"].asString(), UNSUPPORTED_VALUE);
    SerialPort->Close();
}

/**
 * Checks that continuous read mode enabled by the driver is restored after the check.
 */
TEST_F(TRPCContinuousReadTest, RestoresModeEnabledByDriver)
{
    CreateDevice(true);
    EnqueueHoldingRead(CONTINUOUS_READ_ADDRESS, 0);
    EnqueueHoldingWrite(CONTINUOUS_READ_ADDRESS, 1);
    Device->Prepare(*SerialPort, TDevicePrepareMode::WITHOUT_SETUP);
    ASSERT_EQ(Device->GetContinuousReadStatus(), TContinuousReadStatus::ENABLED_TEMPORARY);

    EnqueueHoldingWrite(CONTINUOUS_READ_ADDRESS, 0);
    EnqueueHoldingReadException(PARAMETER_ADDRESS);
    EnqueueHoldingWrite(CONTINUOUS_READ_ADDRESS, 1);

    TRPCDeviceRequest request(DeviceFactory.GetProtocolParams("modbus"), Device, DeviceTemplate, false);
    auto registerList = MakeRegisterList(0xFFFE);
    Json::Value data;
    MarkUnsupportedRegisterItems(*SerialPort, request, registerList, data);

    ASSERT_EQ(data["parameter"].asString(), UNSUPPORTED_VALUE);
    SerialPort->Close();
}

/**
 * Checks that continuous read register is not written if the mode is disabled for the device.
 */
TEST_F(TRPCContinuousReadTest, SkipsDisabledMode)
{
    CreateDevice(false);
    Device->Prepare(*SerialPort, TDevicePrepareMode::WITHOUT_SETUP);
    ASSERT_EQ(Device->GetContinuousReadStatus(), TContinuousReadStatus::DISABLED);

    TRPCDeviceRequest request(DeviceFactory.GetProtocolParams("modbus"), Device, DeviceTemplate, false);
    auto registerList = MakeRegisterList(0xFFFE);
    Json::Value data;
    MarkUnsupportedRegisterItems(*SerialPort, request, registerList, data);

    ASSERT_TRUE(data["parameter"].isNull());
    SerialPort->Close();
}

/**
 * Checks that continuous read register is not written if there are no unsupported values.
 */
TEST_F(TRPCContinuousReadTest, SkipsSupportedValues)
{
    CreateDevice(true);
    EnqueueHoldingRead(CONTINUOUS_READ_ADDRESS, 2);
    Device->Prepare(*SerialPort, TDevicePrepareMode::WITHOUT_SETUP);

    TRPCDeviceRequest request(DeviceFactory.GetProtocolParams("modbus"), Device, DeviceTemplate, false);
    auto registerList = MakeRegisterList(0x1234);
    Json::Value data;
    MarkUnsupportedRegisterItems(*SerialPort, request, registerList, data);

    ASSERT_TRUE(data["parameter"].isNull());
    SerialPort->Close();
}
