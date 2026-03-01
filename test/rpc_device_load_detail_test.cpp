#include <fstream>
#include <gtest/gtest.h>

#include "devices/modbus_device.h"
#include "fake_serial_port.h"
#include "modbus_base.h"
#include "modbus_common.h"
#include "modbus_expectations_base.h"
#include "rpc/rpc_device_handler.h"
#include "rpc/rpc_device_load_detail.h"
#include "rpc/rpc_exception.h"
#include "rpc/rpc_helpers.h"
#include "serial_config.h"
#include "templates_map.h"
#include "wb_registers.h"

namespace
{
    const std::string TestTemplateFile = "/tmp/rpc_device_load_detail_test_template.json";

    PDeviceTemplate CreateMinimalTemplate()
    {
        std::ofstream f(TestTemplateFile);
        f << R"({
            "device_type": "test",
            "device": {
                "name": "Test",
                "id": "test",
                "channels": [],
                "parameters": {}
            }
        })";
        f.close();
        auto tmpl = std::make_shared<TDeviceTemplate>("test", "modbus", nullptr, TestTemplateFile);
        tmpl->SetDeprecated();
        return tmpl;
    }

    PDeviceTemplate CreateTemplateWithChannelAndParam()
    {
        std::ofstream f(TestTemplateFile);
        f << R"({
            "device_type": "test",
            "device": {
                "name": "Test",
                "id": "test",
                "channels": [
                    {
                        "name": "Temperature",
                        "reg_type": "holding",
                        "address": 10,
                        "type": "value",
                        "format": "u16"
                    }
                ],
                "parameters": {
                    "mode": {
                        "title": "Mode",
                        "address": 20,
                        "reg_type": "holding",
                        "format": "u16"
                    }
                }
            }
        })";
        f.close();
        auto tmpl = std::make_shared<TDeviceTemplate>("test", "modbus", nullptr, TestTemplateFile);
        tmpl->SetDeprecated();
        return tmpl;
    }
}

// ============================================================
// Test fixture
// ============================================================

class TRPCDeviceLoadDetailTest: public TSerialDeviceTest, public TModbusExpectationsBase
{
protected:
    void SetUp() override
    {
        SelectModbusType(MODBUS_RTU);
        TSerialDeviceTest::SetUp();

        TModbusDeviceConfig config;
        config.CommonConfig = std::make_shared<TDeviceConfig>("modbus", "1", "modbus");

        ModbusDev = std::make_shared<TModbusDevice>(std::make_unique<Modbus::TModbusRTUTraits>(),
                                                    config,
                                                    DeviceFactory.GetProtocol("modbus"));

        ProtocolParams = DeviceFactory.GetProtocolParams("modbus");

        SerialPort->Open();
    }

    void TearDown() override
    {
        TSerialDeviceTest::TearDown();
        std::remove(TestTemplateFile.c_str());
    }

    // Enqueue write of continuous_read register (address 0x0072) with given value
    void EnqueueWriteContinuousRead(uint16_t value)
    {
        Expector()->Expect(WrapPDU({0x06, 0x00, 0x72, 0x00, value}),
                           WrapPDU({0x06, 0x00, 0x72, 0x00, value}),
                           __func__);
    }

    // Enqueue write timeout for continuous_read register (enable)
    void EnqueueWriteContinuousReadEnableTimeout()
    {
        Expector()->Expect(WrapPDU({0x06, 0x00, 0x72, 0x00, 0x01}), {}, __func__);
    }

    // Enqueue read of a holding register at given address (1 register, U16)
    void EnqueueReadHoldingU16(uint16_t address, uint16_t value)
    {
        uint8_t addrHi = (address >> 8) & 0xFF;
        uint8_t addrLo = address & 0xFF;
        uint8_t valHi = (value >> 8) & 0xFF;
        uint8_t valLo = value & 0xFF;
        Expector()->Expect(WrapPDU({0x03, addrHi, addrLo, 0x00, 0x01}), WrapPDU({0x03, 0x02, valHi, valLo}), __func__);
    }

    // Enqueue modbus exception for read of a holding register
    void EnqueueReadHoldingException(uint16_t address, uint8_t exceptionCode)
    {
        uint8_t addrHi = (address >> 8) & 0xFF;
        uint8_t addrLo = address & 0xFF;
        Expector()->Expect(WrapPDU({0x03, addrHi, addrLo, 0x00, 0x01}), WrapPDU({0x83, exceptionCode}), __func__);
    }

    std::shared_ptr<TModbusDevice> ModbusDev;
    TDeviceProtocolParams ProtocolParams;
};

// ============================================================
// rpc_device_load_detail::SetContinuousRead tests
// ============================================================

TEST_F(TRPCDeviceLoadDetailTest, SetContinuousRead_Enable)
{
    auto tmpl = CreateMinimalTemplate();
    TRPCDeviceRequest request(ProtocolParams, ModbusDev, tmpl, false);

    EnqueueWriteContinuousRead(0x01);

    EXPECT_NO_THROW(rpc_device_load_detail::SetContinuousRead(*SerialPort, request, true));
}

TEST_F(TRPCDeviceLoadDetailTest, SetContinuousRead_Disable)
{
    auto tmpl = CreateMinimalTemplate();
    TRPCDeviceRequest request(ProtocolParams, ModbusDev, tmpl, false);

    EnqueueWriteContinuousRead(0x00);

    EXPECT_NO_THROW(rpc_device_load_detail::SetContinuousRead(*SerialPort, request, false));
}

TEST_F(TRPCDeviceLoadDetailTest, SetContinuousRead_TimeoutLogsWarning)
{
    auto tmpl = CreateMinimalTemplate();
    TRPCDeviceRequest request(ProtocolParams, ModbusDev, tmpl, false);

    // MAX_RPC_RETRIES=2, so 3 attempts total
    EnqueueWriteContinuousReadEnableTimeout();
    EnqueueWriteContinuousReadEnableTimeout();
    EnqueueWriteContinuousReadEnableTimeout();

    // SetContinuousRead in the load variant catches errors and logs, doesn't throw
    EXPECT_NO_THROW(rpc_device_load_detail::SetContinuousRead(*SerialPort, request, true));
}

// ============================================================
// rpc_device_load_detail::MarkUnsupported tests
// ============================================================

TEST_F(TRPCDeviceLoadDetailTest, MarkUnsupported_NoFFFE)
{
    auto tmpl = CreateMinimalTemplate();
    TRPCDeviceRequest request(ProtocolParams, ModbusDev, tmpl, false);

    // Create a register with a normal (non-FFFE) value
    auto regConfig = TRegisterConfig::Create(Modbus::REG_HOLDING, 100, U16);
    auto reg = std::make_shared<TRegister>(ModbusDev, regConfig);
    reg->SetValue(TRegisterValue(uint64_t(0x0042)));

    TRPCRegisterList registerList;
    registerList.push_back({"test_reg", "", reg, true});

    Json::Value data(Json::objectValue);

    // No modbus operations expected — value is not FFFE
    rpc_device_load_detail::MarkUnsupported(*SerialPort, request, registerList, data);

    EXPECT_FALSE(data.isMember("test_reg"));
}

TEST_F(TRPCDeviceLoadDetailTest, MarkUnsupported_FFFEMarkedUnsupported)
{
    auto tmpl = CreateMinimalTemplate();
    TRPCDeviceRequest request(ProtocolParams, ModbusDev, tmpl, false);

    // Create a register with value 0xFFFE and CheckUnsupported=true
    auto regConfig = TRegisterConfig::Create(Modbus::REG_HOLDING, 100, U16);
    auto reg = std::make_shared<TRegister>(ModbusDev, regConfig);
    reg->SetValue(TRegisterValue(uint64_t(0xFFFE)));

    TRPCRegisterList registerList;
    registerList.push_back({"test_reg", "", reg, true});

    Json::Value data(Json::objectValue);

    // Expected sequence:
    // 1. Write continuous_read=0 (disable)
    EnqueueWriteContinuousRead(0x00);
    // 2. Re-read the register — modbus exception (illegal data address)
    EnqueueReadHoldingException(100, Modbus::ILLEGAL_DATA_ADDRESS);
    // 3. Write continuous_read=1 (re-enable)
    EnqueueWriteContinuousRead(0x01);

    rpc_device_load_detail::MarkUnsupported(*SerialPort, request, registerList, data);

    EXPECT_TRUE(data.isMember("test_reg"));
    EXPECT_EQ(data["test_reg"].asString(), "unsupported");
}

TEST_F(TRPCDeviceLoadDetailTest, MarkUnsupported_FFFERereadSucceeds)
{
    auto tmpl = CreateMinimalTemplate();
    TRPCDeviceRequest request(ProtocolParams, ModbusDev, tmpl, false);

    auto regConfig = TRegisterConfig::Create(Modbus::REG_HOLDING, 100, U16);
    auto reg = std::make_shared<TRegister>(ModbusDev, regConfig);
    reg->SetValue(TRegisterValue(uint64_t(0xFFFE)));

    TRPCRegisterList registerList;
    registerList.push_back({"test_reg", "", reg, true});

    Json::Value data(Json::objectValue);

    // 1. Write continuous_read=0
    EnqueueWriteContinuousRead(0x00);
    // 2. Re-read succeeds (no exception) — register is actually supported
    EnqueueReadHoldingU16(100, 0x0042);
    // 3. Write continuous_read=1 (re-enable)
    EnqueueWriteContinuousRead(0x01);

    rpc_device_load_detail::MarkUnsupported(*SerialPort, request, registerList, data);

    // Re-read succeeded, so it should NOT be marked as unsupported
    EXPECT_FALSE(data.isMember("test_reg"));
}

// ============================================================
// rpc_device_load_detail::ExecRPCRequest tests
// ============================================================

TEST_F(TRPCDeviceLoadDetailTest, ExecRPCRequest_NullOnResult)
{
    auto tmpl = CreateMinimalTemplate();
    auto rpcRequest = std::make_shared<TRPCDeviceLoadRequest>(ProtocolParams, ModbusDev, tmpl, false);
    // OnResult is nullptr by default — no modbus operations, early return
    EXPECT_NO_THROW(rpc_device_load_detail::ExecRPCRequest(SerialPort, rpcRequest));
}

TEST_F(TRPCDeviceLoadDetailTest, ExecRPCRequest_FullFlow)
{
    auto tmpl = CreateTemplateWithChannelAndParam();
    auto rpcRequest = std::make_shared<TRPCDeviceLoadRequest>(ProtocolParams, ModbusDev, tmpl, false);
    rpcRequest->Channels.push_back("Temperature");
    rpcRequest->Parameters.push_back("mode");

    Json::Value receivedResult;
    rpcRequest->OnResult = [&receivedResult](const Json::Value& result) { receivedResult = result; };

    // Read channel register at address 10
    EnqueueReadHoldingU16(10, 0x0064); // value = 100
    // Read parameter register at address 20
    EnqueueReadHoldingU16(20, 0x0003); // value = 3

    EXPECT_NO_THROW(rpc_device_load_detail::ExecRPCRequest(SerialPort, rpcRequest));

    EXPECT_TRUE(receivedResult.isMember("channels"));
    EXPECT_TRUE(receivedResult.isMember("parameters"));
    EXPECT_TRUE(receivedResult["channels"].isMember("Temperature"));
    EXPECT_TRUE(receivedResult["parameters"].isMember("mode"));
    EXPECT_EQ(receivedResult["channels"]["Temperature"].asInt(), 100);
    EXPECT_EQ(receivedResult["parameters"]["mode"].asInt(), 3);
}
