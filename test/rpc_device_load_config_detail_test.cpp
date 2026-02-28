#include <fstream>
#include <gtest/gtest.h>

#include "devices/modbus_device.h"
#include "fake_serial_port.h"
#include "modbus_base.h"
#include "modbus_common.h"
#include "modbus_expectations_base.h"
#include "rpc/rpc_device_handler.h"
#include "rpc/rpc_device_load_config_detail.h"
#include "rpc/rpc_exception.h"
#include "rpc/rpc_helpers.h"
#include "serial_config.h"
#include "templates_map.h"
#include "wb_registers.h"

namespace
{
    const std::string TestTemplateFile = "/tmp/rpc_device_load_config_detail_test_template.json";

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

    PDeviceTemplate CreateTemplateWithEmptyParams()
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

    PDeviceTemplate CreateTemplateWithHardware(const std::string& model, const std::string& fw)
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
        tmpl->SetHardware({{model, fw}});
        return tmpl;
    }
}

// ============================================================
// Test fixture
// ============================================================

class TRPCDeviceLoadConfigDetailTest: public TSerialDeviceTest, public TModbusExpectationsBase
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
        Expector()->Expect(
            WrapPDU({0x06, 0x00, 0x72, 0x00, value}),
            WrapPDU({0x06, 0x00, 0x72, 0x00, value}),
            __func__);
    }

    // Enqueue write timeout for continuous_read register (enable)
    void EnqueueWriteContinuousReadEnableTimeout()
    {
        Expector()->Expect(
            WrapPDU({0x06, 0x00, 0x72, 0x00, 0x01}),
            {},
            __func__);
    }

    // Enqueue read of continuous_read register with given response value
    void EnqueueReadContinuousRead(uint16_t value)
    {
        uint8_t hi = (value >> 8) & 0xFF;
        uint8_t lo = value & 0xFF;
        Expector()->Expect(
            WrapPDU({0x03, 0x00, 0x72, 0x00, 0x01}),
            WrapPDU({0x03, 0x02, hi, lo}),
            __func__);
    }

    // Enqueue modbus exception for read of continuous_read register
    void EnqueueReadContinuousReadException(uint8_t exceptionCode)
    {
        Expector()->Expect(
            WrapPDU({0x03, 0x00, 0x72, 0x00, 0x01}),
            WrapPDU({0x83, exceptionCode}),
            __func__);
    }

    // Enqueue read of device_model_ex (20 regs at address 0x00C8) with string response.
    // String format: 1 char per 16-bit register (char in low byte, high byte = 0x00).
    void EnqueueReadDeviceModelEx(const std::string& model)
    {
        std::vector<int> response = {0x03, 40}; // 20 regs = 40 bytes
        for (size_t i = 0; i < 20; ++i) {
            response.push_back(0x00); // high byte
            response.push_back(i < model.size() ? static_cast<uint8_t>(model[i]) : 0); // low byte
        }
        Expector()->Expect(
            WrapPDU({0x03, 0x00, 0xC8, 0x00, 0x14}),
            WrapPDU(response),
            __func__);
    }

    // Enqueue modbus exception for device_model_ex read (20 regs)
    void EnqueueReadDeviceModelExException(uint8_t exceptionCode)
    {
        Expector()->Expect(
            WrapPDU({0x03, 0x00, 0xC8, 0x00, 0x14}),
            WrapPDU({0x83, exceptionCode}),
            __func__);
    }

    // Enqueue read of device_model (6 regs at address 0x00C8) with string response.
    // String format: 1 char per 16-bit register (char in low byte, high byte = 0x00).
    void EnqueueReadDeviceModel(const std::string& model)
    {
        std::vector<int> response = {0x03, 12}; // 6 regs = 12 bytes
        for (size_t i = 0; i < 6; ++i) {
            response.push_back(0x00); // high byte
            response.push_back(i < model.size() ? static_cast<uint8_t>(model[i]) : 0); // low byte
        }
        Expector()->Expect(
            WrapPDU({0x03, 0x00, 0xC8, 0x00, 0x06}),
            WrapPDU(response),
            __func__);
    }

    std::shared_ptr<TModbusDevice> ModbusDev;
    TDeviceProtocolParams ProtocolParams;
};

// ============================================================
// rpc_device_load_config_detail::ReadWbRegister tests
// ============================================================

TEST_F(TRPCDeviceLoadConfigDetailTest, ReadWbRegister_Success)
{
    auto tmpl = CreateMinimalTemplate();
    std::string configFileName = "/tmp/test_config.conf";
    TRPCDeviceParametersCache cache;
    auto rpcRequest =
        std::make_shared<TRPCDeviceLoadConfigRequest>(ProtocolParams, ModbusDev, tmpl, false, configFileName, cache);

    // Read device_model register (String format, 6 regs at 0x00C8)
    EnqueueReadDeviceModel("WB-M3H");

    auto result = rpc_device_load_config_detail::ReadWbRegister(
        *SerialPort, rpcRequest, WbRegisters::DEVICE_MODEL_REGISTER_NAME);
    EXPECT_EQ(result, "WB-M3H");
}

TEST_F(TRPCDeviceLoadConfigDetailTest, ReadWbRegister_ModbusExceptionThrows)
{
    auto tmpl = CreateMinimalTemplate();
    std::string configFileName = "/tmp/test_config.conf";
    TRPCDeviceParametersCache cache;
    auto rpcRequest =
        std::make_shared<TRPCDeviceLoadConfigRequest>(ProtocolParams, ModbusDev, tmpl, false, configFileName, cache);

    // device_model_ex read fails with modbus exception — immediate TRPCException
    EnqueueReadDeviceModelExException(Modbus::ILLEGAL_DATA_ADDRESS);

    EXPECT_THROW(rpc_device_load_config_detail::ReadWbRegister(
                     *SerialPort, rpcRequest, WbRegisters::DEVICE_MODEL_EX_REGISTER_NAME),
                 TRPCException);
}

// ============================================================
// rpc_device_load_config_detail::ReadDeviceModel tests
// ============================================================

TEST_F(TRPCDeviceLoadConfigDetailTest, ReadDeviceModel_ExSuccess)
{
    auto tmpl = CreateMinimalTemplate();
    std::string configFileName = "/tmp/test_config.conf";
    TRPCDeviceParametersCache cache;
    auto rpcRequest =
        std::make_shared<TRPCDeviceLoadConfigRequest>(ProtocolParams, ModbusDev, tmpl, false, configFileName, cache);

    EnqueueReadDeviceModelEx("WB-MAP3H");

    auto model = rpc_device_load_config_detail::ReadDeviceModel(*SerialPort, rpcRequest);
    EXPECT_EQ(model, "WB-MAP3H");
}

TEST_F(TRPCDeviceLoadConfigDetailTest, ReadDeviceModel_ExFails_ThrowsRPCException)
{
    auto tmpl = CreateMinimalTemplate();
    std::string configFileName = "/tmp/test_config.conf";
    TRPCDeviceParametersCache cache;
    auto rpcRequest =
        std::make_shared<TRPCDeviceLoadConfigRequest>(ProtocolParams, ModbusDev, tmpl, false, configFileName, cache);

    // device_model_ex fails with modbus exception.
    // ReadWbRegister wraps the error as TRPCException, which ReadDeviceModel's
    // catch(Modbus::TErrorBase&) doesn't match — so TRPCException propagates.
    EnqueueReadDeviceModelExException(Modbus::ILLEGAL_DATA_ADDRESS);

    EXPECT_THROW(rpc_device_load_config_detail::ReadDeviceModel(*SerialPort, rpcRequest), TRPCException);
}

// ============================================================
// rpc_device_load_config_detail::SetContinuousRead tests (throws on failure)
// ============================================================

TEST_F(TRPCDeviceLoadConfigDetailTest, SetContinuousRead_Config_Success)
{
    auto tmpl = CreateMinimalTemplate();
    std::string configFileName = "/tmp/test_config.conf";
    TRPCDeviceParametersCache cache;
    auto rpcRequest =
        std::make_shared<TRPCDeviceLoadConfigRequest>(ProtocolParams, ModbusDev, tmpl, false, configFileName, cache);

    EnqueueWriteContinuousRead(0x01);

    EXPECT_NO_THROW(rpc_device_load_config_detail::SetContinuousRead(*SerialPort, rpcRequest, true));
}

TEST_F(TRPCDeviceLoadConfigDetailTest, SetContinuousRead_Config_TimeoutThrows)
{
    auto tmpl = CreateMinimalTemplate();
    std::string configFileName = "/tmp/test_config.conf";
    TRPCDeviceParametersCache cache;
    auto rpcRequest =
        std::make_shared<TRPCDeviceLoadConfigRequest>(ProtocolParams, ModbusDev, tmpl, false, configFileName, cache);

    // MAX_RPC_RETRIES=2, so 3 attempts total
    EnqueueWriteContinuousReadEnableTimeout();
    EnqueueWriteContinuousReadEnableTimeout();
    EnqueueWriteContinuousReadEnableTimeout();

    // Config variant throws TRPCException on failure
    EXPECT_THROW(rpc_device_load_config_detail::SetContinuousRead(*SerialPort, rpcRequest, true), TRPCException);
}

// ============================================================
// rpc_device_load_config_detail::CheckTemplate tests
// ============================================================

TEST_F(TRPCDeviceLoadConfigDetailTest, CheckTemplate_Matching)
{
    auto tmpl = CreateTemplateWithHardware("WB-MAP3H", "1.0.0");
    std::string configFileName = "/tmp/test_config.conf";
    TRPCDeviceParametersCache cache;
    auto rpcRequest =
        std::make_shared<TRPCDeviceLoadConfigRequest>(ProtocolParams, ModbusDev, tmpl, false, configFileName, cache);
    rpcRequest->Device->SetWbFwVersion("2.0.0");

    std::string model = "WB-MAP3H";
    // Model is already provided, no modbus reads needed. FW 2.0.0 >= required 1.0.0, should pass.
    EXPECT_NO_THROW(rpc_device_load_config_detail::CheckTemplate(SerialPort, rpcRequest, model));
}

TEST_F(TRPCDeviceLoadConfigDetailTest, CheckTemplate_FwTooLow)
{
    auto tmpl = CreateTemplateWithHardware("WB-MAP3H", "1.0.0");
    std::string configFileName = "/tmp/test_config.conf";
    TRPCDeviceParametersCache cache;
    auto rpcRequest =
        std::make_shared<TRPCDeviceLoadConfigRequest>(ProtocolParams, ModbusDev, tmpl, false, configFileName, cache);
    rpcRequest->Device->SetWbFwVersion("0.5.0");

    std::string model = "WB-MAP3H";
    // FW 0.5.0 < required 1.0.0 — should throw
    EXPECT_THROW(rpc_device_load_config_detail::CheckTemplate(SerialPort, rpcRequest, model), TRPCException);
}

TEST_F(TRPCDeviceLoadConfigDetailTest, CheckTemplate_IncompatibleModel)
{
    auto tmpl = CreateTemplateWithHardware("WB-MAP3H", "1.0.0");
    std::string configFileName = "/tmp/test_config.conf";
    TRPCDeviceParametersCache cache;
    auto rpcRequest =
        std::make_shared<TRPCDeviceLoadConfigRequest>(ProtocolParams, ModbusDev, tmpl, false, configFileName, cache);
    rpcRequest->Device->SetWbFwVersion("2.0.0");

    std::string model = "WRONG-MODEL";
    // Model doesn't match any hardware signature — should throw
    EXPECT_THROW(rpc_device_load_config_detail::CheckTemplate(SerialPort, rpcRequest, model), TRPCException);
}

// ============================================================
// rpc_device_load_config_detail::ExecRPCRequest tests
// ============================================================

TEST_F(TRPCDeviceLoadConfigDetailTest, ExecRPCRequest_Config_NullOnResult)
{
    auto tmpl = CreateMinimalTemplate();
    std::string configFileName = "/tmp/test_config.conf";
    TRPCDeviceParametersCache cache;
    auto rpcRequest =
        std::make_shared<TRPCDeviceLoadConfigRequest>(ProtocolParams, ModbusDev, tmpl, false, configFileName, cache);
    // OnResult is nullptr by default — early return, no modbus ops
    EXPECT_NO_THROW(rpc_device_load_config_detail::ExecRPCRequest(SerialPort, rpcRequest));
}

TEST_F(TRPCDeviceLoadConfigDetailTest, ExecRPCRequest_Config_EmptyParams)
{
    auto tmpl = CreateTemplateWithEmptyParams();
    std::string configFileName = "/tmp/test_config.conf";
    TRPCDeviceParametersCache cache;
    auto rpcRequest =
        std::make_shared<TRPCDeviceLoadConfigRequest>(ProtocolParams, ModbusDev, tmpl, false, configFileName, cache);

    Json::Value receivedResult;
    rpcRequest->OnResult = [&receivedResult](const Json::Value& result) { receivedResult = result; };

    // Template has empty parameters — should call OnResult with empty object immediately
    EXPECT_NO_THROW(rpc_device_load_config_detail::ExecRPCRequest(SerialPort, rpcRequest));

    EXPECT_TRUE(receivedResult.isObject());
    EXPECT_TRUE(receivedResult.empty());
}
