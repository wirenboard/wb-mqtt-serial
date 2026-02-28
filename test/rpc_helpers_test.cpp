#include <fstream>
#include <gtest/gtest.h>

#include "devices/modbus_device.h"
#include "fake_serial_port.h"
#include "modbus_base.h"
#include "modbus_common.h"
#include "modbus_expectations_base.h"
#include "rpc/rpc_device_handler.h"
#include "rpc/rpc_exception.h"
#include "rpc/rpc_helpers.h"
#include "serial_config.h"
#include "templates_map.h"

// ============================================================
// IsAllFFFE tests
// ============================================================

TEST(TRPCHelpersTest, IsAllFFFE_IntegerFFFE)
{
    EXPECT_TRUE(IsAllFFFE(TRegisterValue(uint64_t(0xFFFE))));
}

TEST(TRPCHelpersTest, IsAllFFFE_IntegerOther)
{
    EXPECT_FALSE(IsAllFFFE(TRegisterValue(uint64_t(0x1234))));
}

TEST(TRPCHelpersTest, IsAllFFFE_IntegerZero)
{
    EXPECT_FALSE(IsAllFFFE(TRegisterValue(uint64_t(0))));
}

TEST(TRPCHelpersTest, IsAllFFFE_StringAllFE)
{
    EXPECT_TRUE(IsAllFFFE(TRegisterValue(std::string("\xFE\xFE\xFE"))));
}

TEST(TRPCHelpersTest, IsAllFFFE_StringEmpty)
{
    EXPECT_TRUE(IsAllFFFE(TRegisterValue(std::string(""))));
}

TEST(TRPCHelpersTest, IsAllFFFE_StringMixed)
{
    EXPECT_FALSE(IsAllFFFE(TRegisterValue(std::string("\xFE\x01\xFE"))));
}

TEST(TRPCHelpersTest, IsAllFFFE_StringNormal)
{
    EXPECT_FALSE(IsAllFFFE(TRegisterValue(std::string("hello"))));
}

TEST(TRPCHelpersTest, IsAllFFFE_Undefined)
{
    EXPECT_FALSE(IsAllFFFE(TRegisterValue()));
}

// ============================================================
// ParseRPCSerialPortSettings tests
// ============================================================

TEST(TRPCHelpersTest, ParseSerialSettings_AllFields)
{
    Json::Value request;
    request["baud_rate"] = 115200;
    request["parity"] = "E";
    request["data_bits"] = 7;
    request["stop_bits"] = 2;

    auto settings = ParseRPCSerialPortSettings(request);

    EXPECT_EQ(settings.BaudRate, 115200);
    EXPECT_EQ(settings.Parity, 'E');
    EXPECT_EQ(settings.DataBits, 7);
    EXPECT_EQ(settings.StopBits, 2);
}

TEST(TRPCHelpersTest, ParseSerialSettings_Empty)
{
    Json::Value request(Json::objectValue);

    auto settings = ParseRPCSerialPortSettings(request);

    EXPECT_EQ(settings.BaudRate, 9600);
    EXPECT_EQ(settings.Parity, 'N');
    EXPECT_EQ(settings.DataBits, 8);
    EXPECT_EQ(settings.StopBits, 1);
}

TEST(TRPCHelpersTest, ParseSerialSettings_Partial)
{
    Json::Value request;
    request["baud_rate"] = 19200;

    auto settings = ParseRPCSerialPortSettings(request);

    EXPECT_EQ(settings.BaudRate, 19200);
    EXPECT_EQ(settings.Parity, 'N');
    EXPECT_EQ(settings.DataBits, 8);
    EXPECT_EQ(settings.StopBits, 1);
}

// ============================================================
// ValidateRPCRequest tests
// ============================================================

TEST(TRPCHelpersTest, ValidateRPCRequest_Valid)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["name"]["type"] = "string";
    Json::Value required(Json::arrayValue);
    required.append("name");
    schema["required"] = required;

    Json::Value request;
    request["name"] = "test";

    EXPECT_NO_THROW(ValidateRPCRequest(request, schema));
}

TEST(TRPCHelpersTest, ValidateRPCRequest_Invalid)
{
    Json::Value schema;
    schema["type"] = "object";
    schema["properties"]["name"]["type"] = "string";
    Json::Value required(Json::arrayValue);
    required.append("name");
    schema["required"] = required;

    Json::Value request(Json::objectValue);

    try {
        ValidateRPCRequest(request, schema);
        FAIL() << "Expected TRPCException";
    } catch (const TRPCException& e) {
        EXPECT_EQ(e.GetResultCode(), TRPCResultCode::RPC_WRONG_PARAM_VALUE);
    }
}

// ============================================================
// LoadRPCRequestSchema tests
// ============================================================

TEST(TRPCHelpersTest, LoadRPCRequestSchema_Success)
{
    auto schema = LoadRPCRequestSchema("wb-mqtt-serial-rpc-port-setup-request.schema.json", "port/Setup");

    EXPECT_TRUE(schema.isObject());
    EXPECT_TRUE(schema.isMember("type"));
    EXPECT_EQ(schema["type"].asString(), "object");
}

TEST(TRPCHelpersTest, LoadRPCRequestSchema_NotFound)
{
    EXPECT_THROW(LoadRPCRequestSchema("nonexistent-file.schema.json", "test/Nonexistent"), std::runtime_error);
}

// ============================================================
// ReadModbusRegister / WriteModbusRegister tests
// ============================================================

namespace
{
    const std::string TestTemplateFile = "/tmp/rpc_helpers_test_template.json";

    PDeviceTemplate CreateMinimalTemplate()
    {
        std::ofstream f(TestTemplateFile);
        f << R"({"device_type":"test","device":{"name":"Test","id":"test","channels":[]}})";
        f.close();

        auto tmpl = std::make_shared<TDeviceTemplate>("test", "modbus", nullptr, TestTemplateFile);
        tmpl->SetDeprecated();
        return tmpl;
    }
}

class TRPCHelpersModbusTest: public TSerialDeviceTest, public TModbusExpectationsBase
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

        HoldingReg = TRegisterConfig::Create(Modbus::REG_HOLDING, 70, U16);

        auto protocolParams = DeviceFactory.GetProtocolParams("modbus");
        auto tmpl = CreateMinimalTemplate();
        Request = std::make_unique<TRPCDeviceRequest>(protocolParams, ModbusDev, tmpl, false);

        SerialPort->Open();
    }

    void TearDown() override
    {
        Request.reset();
        TSerialDeviceTest::TearDown();
        std::remove(TestTemplateFile.c_str());
    }

    void EnqueueReadResponse()
    {
        Expector()->Expect(WrapPDU({0x03, 0x00, 70, 0x00, 0x01}), WrapPDU({0x03, 0x02, 0x00, 0x15}), __func__);
    }

    void EnqueueReadTimeout()
    {
        Expector()->Expect(WrapPDU({0x03, 0x00, 70, 0x00, 0x01}), {}, __func__);
    }

    void EnqueueReadException(uint8_t exceptionCode)
    {
        Expector()->Expect(WrapPDU({0x03, 0x00, 70, 0x00, 0x01}), WrapPDU({0x83, exceptionCode}), __func__);
    }

    void EnqueueWriteResponse()
    {
        Expector()->Expect(WrapPDU({0x06, 0x00, 70, 0x00, 0x42}), WrapPDU({0x06, 0x00, 70, 0x00, 0x42}), __func__);
    }

    void EnqueueWriteTimeout()
    {
        Expector()->Expect(WrapPDU({0x06, 0x00, 70, 0x00, 0x42}), {}, __func__);
    }

    void EnqueueWriteException(uint8_t exceptionCode)
    {
        Expector()->Expect(WrapPDU({0x06, 0x00, 70, 0x00, 0x42}), WrapPDU({0x86, exceptionCode}), __func__);
    }

    std::shared_ptr<TModbusDevice> ModbusDev;
    PRegisterConfig HoldingReg;
    std::unique_ptr<TRPCDeviceRequest> Request;
};

// --- ReadModbusRegister tests ---
// Modbus::ReadRegister throws TModbusExceptionError directly (no wrapper).

TEST_F(TRPCHelpersModbusTest, ReadModbusRegister_Success)
{
    EnqueueReadResponse();

    TRegisterValue value;
    EXPECT_NO_THROW(ReadModbusRegister(*SerialPort, *Request, HoldingReg, value));
    EXPECT_EQ(value.Get<uint64_t>(), 0x0015u);
}

TEST_F(TRPCHelpersModbusTest, ReadModbusRegister_RetryOnTransientError)
{
    EnqueueReadTimeout();
    EnqueueReadResponse();

    TRegisterValue value;
    EXPECT_NO_THROW(ReadModbusRegister(*SerialPort, *Request, HoldingReg, value));
    EXPECT_EQ(value.Get<uint64_t>(), 0x0015u);
}

TEST_F(TRPCHelpersModbusTest, ReadModbusRegister_ThrowAfterMaxRetries)
{
    EnqueueReadTimeout();
    EnqueueReadTimeout();
    EnqueueReadTimeout();

    TRegisterValue value;
    EXPECT_THROW(ReadModbusRegister(*SerialPort, *Request, HoldingReg, value), TResponseTimeoutException);
}

TEST_F(TRPCHelpersModbusTest, ReadModbusRegister_ImmediateThrowOnIllegalFunction)
{
    EnqueueReadException(Modbus::ILLEGAL_FUNCTION);

    TRegisterValue value;
    EXPECT_THROW(ReadModbusRegister(*SerialPort, *Request, HoldingReg, value), Modbus::TModbusExceptionError);
}

TEST_F(TRPCHelpersModbusTest, ReadModbusRegister_ImmediateThrowOnIllegalDataAddress)
{
    EnqueueReadException(Modbus::ILLEGAL_DATA_ADDRESS);

    TRegisterValue value;
    EXPECT_THROW(ReadModbusRegister(*SerialPort, *Request, HoldingReg, value), Modbus::TModbusExceptionError);
}

TEST_F(TRPCHelpersModbusTest, ReadModbusRegister_ImmediateThrowOnIllegalDataValue)
{
    EnqueueReadException(Modbus::ILLEGAL_DATA_VALUE);

    TRegisterValue value;
    EXPECT_THROW(ReadModbusRegister(*SerialPort, *Request, HoldingReg, value), Modbus::TModbusExceptionError);
}

TEST_F(TRPCHelpersModbusTest, ReadModbusRegister_RetryOnOtherModbusException)
{
    // Non-illegal exception codes (e.g. SLAVE_DEVICE_FAILURE=4) are retried
    EnqueueReadException(0x04); // Slave Device Failure
    EnqueueReadResponse();

    TRegisterValue value;
    EXPECT_NO_THROW(ReadModbusRegister(*SerialPort, *Request, HoldingReg, value));
    EXPECT_EQ(value.Get<uint64_t>(), 0x0015u);
}

TEST_F(TRPCHelpersModbusTest, ReadModbusRegister_ThrowOtherModbusExceptionAfterRetries)
{
    // Non-illegal exception codes are retried up to MAX_RPC_RETRIES, then thrown
    EnqueueReadException(0x04);
    EnqueueReadException(0x04);
    EnqueueReadException(0x04);

    TRegisterValue value;
    EXPECT_THROW(ReadModbusRegister(*SerialPort, *Request, HoldingReg, value), Modbus::TModbusExceptionError);
}

// --- WriteModbusRegister tests ---
// Modbus::WriteRegister internally catches TModbusExceptionError and rethrows as
// TSerialDevicePermanentRegisterException (for illegal function/address/value) or
// TSerialDeviceTransientErrorException (for other modbus errors).

TEST_F(TRPCHelpersModbusTest, WriteModbusRegister_Success)
{
    EnqueueWriteResponse();

    TRegisterValue value(uint64_t(0x0042));
    EXPECT_NO_THROW(WriteModbusRegister(*SerialPort, *Request, HoldingReg, value));
}

TEST_F(TRPCHelpersModbusTest, WriteModbusRegister_RetryOnTransientError)
{
    EnqueueWriteTimeout();
    EnqueueWriteResponse();

    TRegisterValue value(uint64_t(0x0042));
    EXPECT_NO_THROW(WriteModbusRegister(*SerialPort, *Request, HoldingReg, value));
}

TEST_F(TRPCHelpersModbusTest, WriteModbusRegister_ThrowAfterMaxRetries)
{
    EnqueueWriteTimeout();
    EnqueueWriteTimeout();
    EnqueueWriteTimeout();

    TRegisterValue value(uint64_t(0x0042));
    EXPECT_THROW(WriteModbusRegister(*SerialPort, *Request, HoldingReg, value), TResponseTimeoutException);
}

TEST_F(TRPCHelpersModbusTest, WriteModbusRegister_ImmediateThrowOnIllegalFunction)
{
    EnqueueWriteException(Modbus::ILLEGAL_FUNCTION);

    TRegisterValue value(uint64_t(0x0042));
    // WriteRegister internally converts TModbusExceptionError to TSerialDevicePermanentRegisterException
    EXPECT_THROW(WriteModbusRegister(*SerialPort, *Request, HoldingReg, value),
                 TSerialDevicePermanentRegisterException);
}
